#include "wlTerrain.h"
#include "wlTerrainPrivate.h"
#include "StringCache.h"
#include "ScratchStack.h"
#include "WorldGridPrivate.h"
#include "FolderCache.h"
#include "tiff.h"
#include "timing.h"
#include "WorldColl.h"
#include "PhysicsSDK.h"
#include "wlState.h"
#include "zutils.h"
#include "WorldCellEntryPrivate.h"
#include "WorldGridLoadPrivate.h"
#include "MemRef.h"

#include "wlTerrain_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Terrain_System););

MP_DEFINE(HeightMap);

static int default_heightmap_lod = 7; // TomY TODO NUKE

static int height_map_system_mod_time;

static WorldCollMaterial* wcMaterial;

static CRITICAL_SECTION terrain_critical_section;

const TerrainFileDef heightmap_def = { "hmp", -1, -1, 0, 4 };
const TerrainFileDef colormap_def = { "clr", -1, -1, 0, 3 };
const TerrainFileDef tiffcolormap_def = { "tiff", -1, -1, 0, 3 };
const TerrainFileDef materialmap_def = { "mat", -1, -1, 0, 8 };
const TerrainFileDef holemap_def = { "alm", -1, -1, 0, 1 };
const TerrainFileDef objectmap_def = { "tom", -1, -1, 0, 0 };
const TerrainFileDef soildepthmap_def = { "soil", -1, -1, 0, 4 };


AUTO_RUN;
void InitTerrain(void)
{
	InitializeCriticalSection(&terrain_critical_section);
}

void terrainLock()
{
	EnterCriticalSection(&terrain_critical_section);
}

void terrainUnlock()
{
	LeaveCriticalSection(&terrain_critical_section);
}

void *terrainAlloc_dbg(int count, int size MEM_DBG_PARMS)
{
#ifdef TERRAIN_CANFAIL_ALLOC
	void *ret;
	ret = calloc_timed_canfail(count, size, _NORMAL_BLACK MEM_DBG_PARMS_CALL);
	terrainGetProcessMemory();
	if (!ret)
	{
		Alertf("Ran out of memory! Aborting painting operation.");
		return NULL;
	}
	return ret;
#else
	return _calloc_dbg(count, size, _NORMAL_BLOCK MEM_DBG_PARMS_CALL);
#endif
}

/********************************
   Buffer handling
 ********************************/

void ClearTerrainBuffer(ZoneMapLayer *layer, TerrainBuffer *buffer)
{
	int i;
	int width = buffer->size;
	switch (buffer->type)
	{
	case TERRAIN_BUFFER_HEIGHT:
		for (i = 0; i < width*width; i++)
		{
			buffer->data_f32[i] = 0.f;
			*((U32 *)&buffer->data_f32[i]) |= 1;
		}
		break;
    case TERRAIN_BUFFER_SELECTION:
		for (i = 0; i < width*width; i++)
			buffer->data_f32[i] = 0;
		break;
	case TERRAIN_BUFFER_SOIL_DEPTH:
		for (i = 0; i < width*width; i++)
			buffer->data_f32[i] = 15.0f;//Arbitrary Number
		break;
	case TERRAIN_BUFFER_ALPHA:
		for (i = 0; i < width*width; i++)
			buffer->data_byte[i] = 0;
		break;
    case TERRAIN_BUFFER_SHADOW:
		for (i = 0; i < width*width; i++)
			buffer->data_byte[i] = 255;
		break;
	case TERRAIN_BUFFER_COLOR:
		for (i = 0; i < width*width; i++)
		{
			setVec3same(buffer->data_u8vec3[i],127);
		}
		break;
	case TERRAIN_BUFFER_MATERIAL:
		{
			int x, z;
			for (z = 0; z < buffer->size; z++)
			{
				for (x = 0; x < buffer->size; x++)
				{
					memset(&buffer->data_material[x+z*buffer->size], 0, sizeof(buffer->data_material[0]));
					buffer->data_material[x+z*buffer->size].weights[0] = 255.f;
				}
			}
			break;
		}
	case TERRAIN_BUFFER_NORMAL:
		break;
	case TERRAIN_BUFFER_OBJECTS:
		for(i=0; i < eaSize(&buffer->data_objects); i++)
		{
			memset(buffer->data_objects[i]->density, 0, sizeof(TerrainObjectDensity)*width*width);
		}
		break;
    case TERRAIN_BUFFER_OCCLUSION:
        break;
	case TERRAIN_BUFFER_ATTR:
		break;
	default:
		Errorf("Unknown terrain buffer type!");
		assert(0);
	};
}

TerrainBuffer *CreateTerrainBuffer_dbg(TerrainBufferType type, int width, int lod MEM_DBG_PARMS)
{
	int i,j;
	TerrainBuffer *ret = calloc(1, sizeof(TerrainBuffer));
	ret->size = width;
	ret->lod = lod;
	ret->type = type;

	switch (type)
	{
	case TERRAIN_BUFFER_HEIGHT:
	case TERRAIN_BUFFER_SOIL_DEPTH:
    case TERRAIN_BUFFER_SELECTION:
		ret->data_f32 = scalloc(width*width, sizeof(F32));
		if (type == TERRAIN_BUFFER_HEIGHT)
			for (i = 0; i < width*width; i++)
				*((U32 *)&ret->data_f32[i]) |= 1;
		else if(type == TERRAIN_BUFFER_SOIL_DEPTH)
			for (i = 0; i < width*width; i++)
				ret->data_f32[i] = 15.0f;//Arbitrary Number
		break;
	case TERRAIN_BUFFER_ALPHA:
		ret->data_byte = scalloc(width*width, sizeof(U8));
		for (i = 0; i < width*width; i++)
			ret->data_byte[i] = 255.f;
		break;
	case TERRAIN_BUFFER_COLOR:
		ret->data_u8vec3 = scalloc(width*width, sizeof(U8Vec3));
		for (i = 0; i < width*width; i++)
		{
			setVec3same(ret->data_u8vec3[i],127);
		}
		break;
	case TERRAIN_BUFFER_MATERIAL:
		ret->data_material = scalloc(width*width, sizeof(TerrainMaterialWeight));
		for (i = 0; i < width*width; i++)
		{
			ret->data_material[i].weights[0] = 255.f;
			for(j = 1; j < NUM_MATERIALS; j++)
			{
				ret->data_material[i].weights[j] = 0;
			}
		}
		break;
	case TERRAIN_BUFFER_NORMAL:
		ret->data_normal = scalloc(width*width, sizeof(TerrainCompressedNormal));
		break;
	case TERRAIN_BUFFER_OCCLUSION:
	case TERRAIN_BUFFER_ATTR:
    case TERRAIN_BUFFER_SHADOW:
		ret->data_byte = scalloc(width*width, 1);
		break;
	case TERRAIN_BUFFER_OBJECTS: 
		ret->data_objects = NULL;
		break;
	default:
		Errorf("Unknown terrain buffer type!");
		assert(0);
	};

	return ret;
}


void DestroyTerrainObjectBuffer(TerrainObjectBuffer *sub_buffer)
{
	SAFE_FREE(sub_buffer->density);
	SAFE_FREE(sub_buffer);	
}

void DestroyTerrainBuffer(TerrainBuffer *buffer)
{
	if(buffer->type == TERRAIN_BUFFER_OBJECTS)
	{
		eaDestroyEx(&buffer->data_objects, DestroyTerrainObjectBuffer);
	}
	else
	{
		SAFE_FREE(buffer->data_byte);
	}
	SAFE_FREE(buffer);
}

//This function does not work the same for TERRAIN_BUFFER_OBJECTS
U32 GetTerrainBufferStride(TerrainBufferType type)
{
	switch (type)
	{
	case TERRAIN_BUFFER_HEIGHT:
	case TERRAIN_BUFFER_SOIL_DEPTH:
    case TERRAIN_BUFFER_SELECTION:
		return sizeof(F32);
	case TERRAIN_BUFFER_ALPHA:
	case TERRAIN_BUFFER_OCCLUSION:
	case TERRAIN_BUFFER_ATTR:
    case TERRAIN_BUFFER_SHADOW:
		return sizeof(U8);
	case TERRAIN_BUFFER_COLOR:
		return sizeof(U8Vec3);
	case TERRAIN_BUFFER_MATERIAL:
		return sizeof(TerrainMaterialWeight);
	case TERRAIN_BUFFER_NORMAL:
		return sizeof(TerrainCompressedNormal);
	case TERRAIN_BUFFER_OBJECTS:
		return sizeof(TerrainObjectDensity);
	default:
		Errorf("Unknown terrain buffer type!");
		assert(0);
	};
	return 1;
}

U32 GetTerrainBufferSize(TerrainBuffer *buffer)
{
	int i;
	U32 stride = GetTerrainBufferStride(buffer->type);
	if(buffer->type == TERRAIN_BUFFER_OBJECTS)
	{
		U32 actual_stride = 0;
		for(i=0; i < eaSize(&buffer->data_objects); i++)
		{
			if(buffer->data_objects[i]->density)
				actual_stride += stride;
		}
		stride = actual_stride;
	}
	return stride*buffer->size*buffer->size;
}

const char *TerrainBufferGetTypeName(TerrainBufferType type)
{
	switch (type)
	{
	case TERRAIN_BUFFER_HEIGHT:
		return "HEIGHT";
	case TERRAIN_BUFFER_SOIL_DEPTH:
		return "SOIL DEPTH";
	case TERRAIN_BUFFER_ALPHA:
		return "ALPHA";
	case TERRAIN_BUFFER_COLOR:
		return "COLOR";
	case TERRAIN_BUFFER_MATERIAL:
		return "MATERIAL";
	case TERRAIN_BUFFER_NORMAL:
		return "NORMAL";
	case TERRAIN_BUFFER_OBJECTS:
		return "OBJECTS";
	case TERRAIN_BUFFER_OCCLUSION:
		return "OCCLUSION";
	case TERRAIN_BUFFER_ATTR:
		return "ATTRIBUTE";
    case TERRAIN_BUFFER_SELECTION:
        return "SELECTION";
    case TERRAIN_BUFFER_SHADOW:
        return "SHADOW";
	};
	return "Unknown";
}

TerrainBuffer *heightMapCreateBufferSize_dbg(HeightMap *map, TerrainBufferType type, int lod, int size MEM_DBG_PARMS)
{
	TerrainBuffer *ret = CreateTerrainBuffer_dbg(type, size, lod MEM_DBG_PARMS_CALL);

    assert(lod <= HEIGHTMAP_MAX_LOD);
    
    if (type == TERRAIN_BUFFER_COLOR)
        map->color_buffer_list[lod] = ret;
    else if (type == TERRAIN_BUFFER_NORMAL)
		map->normal_buffer_list[lod] = ret;
    else
		map->buffer_list[type] = ret;

	return ret;
}


TerrainBuffer *heightMapCreateBuffer_dbg(HeightMap *map, TerrainBufferType type, int lod MEM_DBG_PARMS)
{
	TerrainBuffer *ret = CreateTerrainBuffer_dbg(type, GRID_LOD(lod), lod MEM_DBG_PARMS_CALL);

    assert(lod <= HEIGHTMAP_MAX_LOD);
    
    if (type == TERRAIN_BUFFER_COLOR)
    {
        assert(map->color_buffer_list[lod] == NULL);
        map->color_buffer_list[lod] = ret;
    }
    else if (type == TERRAIN_BUFFER_NORMAL)
    {
        assert(map->normal_buffer_list[lod] == NULL);
		map->normal_buffer_list[lod] = ret;
    }
    else
    {
        assert(map->buffer_list[type] == NULL);
		map->buffer_list[type] = ret;
    }

	return ret;
}

TerrainBuffer *heightMapGetOldestBuffer(HeightMap *map, TerrainBufferType type, int lod)
{
	if (!map)
		return NULL;
	if(map->backup_buffer[type])
		return map->backup_buffer[type];
	return heightMapGetBuffer(map, type, lod);
}

TerrainBuffer *heightMapGetBuffer(HeightMap *map, TerrainBufferType type, int lod)
{
	if (!map)
		return NULL;
    assert(lod <= HEIGHTMAP_MAX_LOD);
    if (type == TERRAIN_BUFFER_COLOR)
		return (lod >= 0 && lod <= HEIGHTMAP_MAX_LOD) ? map->color_buffer_list[lod] : NULL;
    else if (type == TERRAIN_BUFFER_NORMAL)
		return (lod >= 0 && lod <= HEIGHTMAP_MAX_LOD) ? map->normal_buffer_list[lod] : NULL;
	if (map->buffer_list[type] &&
		(lod == -1 || map->buffer_list[type]->lod == lod))
		return map->buffer_list[type];
	return NULL;
}

TerrainBuffer *heightMapDetachBuffer(HeightMap *map, TerrainBufferType type, int lod)
{
    TerrainBuffer *ret = NULL;
    assert(lod <= HEIGHTMAP_MAX_LOD);
    if (type == TERRAIN_BUFFER_COLOR)
    {
		if (lod >= 0 && lod <= HEIGHTMAP_MAX_LOD)
		{
			ret = map->color_buffer_list[lod];
			map->color_buffer_list[lod] = NULL;
		}
		return ret;
    }
    else if (type == TERRAIN_BUFFER_NORMAL)
    {
		if (lod >= 0 && lod <= HEIGHTMAP_MAX_LOD)
		{
			ret = map->normal_buffer_list[lod];
			map->normal_buffer_list[lod] = NULL;
		}
        return ret;
    }
	if (map->buffer_list[type] &&
		(lod < 0 || lod == map->buffer_list[type]->lod))
	{
		ret = map->buffer_list[type];
		map->buffer_list[type] = NULL;
	}
    return ret;
}

void heightMapDeleteBuffer(HeightMap *map, TerrainBufferType type, int lod)
{
    assert(lod <= HEIGHTMAP_MAX_LOD);
    if (type == TERRAIN_BUFFER_COLOR)
    {
        if (map->color_buffer_list[lod])
        {
            DestroyTerrainBuffer(map->color_buffer_list[lod]);
            map->color_buffer_list[lod] = NULL;
        }
    }
    else if (type == TERRAIN_BUFFER_NORMAL)
    {
        if (map->normal_buffer_list[lod])
        {
            DestroyTerrainBuffer(map->normal_buffer_list[lod]);
            map->normal_buffer_list[lod] = NULL;
        }
    }
    else
    {
        if (map->buffer_list[type] &&
			(lod == -1 || lod == map->buffer_list[type]->lod))
        {
            DestroyTerrainBuffer(map->buffer_list[type]);
            map->buffer_list[type] = NULL;
        }
    }
}

void heightMapMakeBackup_dbg(HeightMap *map, int x, int y, int draw_lod, TerrainBufferType type, int object_type/*only used for TERRAIN_BUFFER_OBJECTS*/ MEM_DBG_PARMS)
{
	int i, j;
	int touch_buffer_size = GRID_LOD(draw_lod);
	int loaded_lod = map->loaded_level_of_detail;
	int lod_diff = draw_lod - loaded_lod;

	if(lod_diff != 0)
	{
		x = x>>lod_diff;
		y = y>>lod_diff;
		assert(x < touch_buffer_size && y < touch_buffer_size);

		if(!map->touch_buffer)
			map->touch_buffer = terrainAlloc(touch_buffer_size*touch_buffer_size, sizeof(U8));
		if (!map->touch_buffer)
			return;
		map->touch_buffer[x + y*touch_buffer_size] = 1;
	}

	if (layerIsGenesis(map->zone_map_layer))
		return;

	if(type == TERRAIN_BUFFER_COLOR)
		loaded_lod = GET_COLOR_LOD(loaded_lod);
	else if (type == TERRAIN_BUFFER_OCCLUSION)
		loaded_lod = 5;

	//If we dont have a backup buffer of this type then make one
	if(!map->backup_buffer[type])
	{
		//Search through buffers on the map for the correct one
        TerrainBuffer *buffer = heightMapGetBuffer(map, type, loaded_lod);
		if (!buffer && (type == TERRAIN_BUFFER_ALPHA || type == TERRAIN_BUFFER_OCCLUSION))
		{
			buffer = heightMapCreateBuffer(map, type, loaded_lod);
		}
        if (buffer)
        {
			TerrainBuffer *backup_buffer;
            backup_buffer = CreateTerrainBuffer_dbg(	type, buffer->size, buffer->lod MEM_DBG_PARMS_CALL);
            if(type != TERRAIN_BUFFER_OBJECTS)
            {
                //copy data in and be done
                U32 stride = GetTerrainBufferStride(type);
                memcpy(	backup_buffer->data_byte, 
                        buffer->data_byte, 
                        stride * backup_buffer->size * backup_buffer->size);
				map->backup_buffer[type] = backup_buffer;
                return;
            }
            else
            {
                //Make room for all possible objects that could be drawn but dont backup yet
                for(j=0; j < eaSize(&buffer->data_objects); j++)
                {
                    TerrainObjectBuffer *new_sub_buffer = terrainAlloc(1, sizeof(TerrainObjectBuffer));	
					if (!new_sub_buffer)
					{
						map->backup_buffer[type] = backup_buffer;
						return;
					}
                    new_sub_buffer->object_type = buffer->data_objects[j]->object_type;
                    new_sub_buffer->density = NULL;
                    eaPush(&backup_buffer->data_objects, new_sub_buffer);
                }
            }
			map->backup_buffer[type] = backup_buffer;
        }
        else
            assert("Terrain buffer matching backup buffer is missing!" == 0);
	}

	//If object buffer then we have to check sub buffers
	if(type == TERRAIN_BUFFER_OBJECTS && map->backup_buffer[type])
	{
		//Search to see if we have a back buffer for the sub buffer
		for(i=0; i < eaSize(&map->backup_buffer[type]->data_objects); i++)
		{
			TerrainObjectBuffer *object_buffer = map->backup_buffer[type]->data_objects[i];
			if(object_buffer->object_type == object_type)
			{
				//If we dont have a back up buffer
				if(object_buffer->density == NULL)
				{
					//Find the buffer on the heightmap
                    TerrainBuffer *buffer = map->buffer_list[TERRAIN_BUFFER_OBJECTS];
                    if (buffer)
                    {
                        //copy data
                        assert(i < eaSize(&buffer->data_objects));
                        assert(buffer->data_objects[i]->object_type == object_type);
                        object_buffer->density = malloc(buffer->size*buffer->size*sizeof(TerrainObjectDensity));
                        memcpy(object_buffer->density, buffer->data_objects[i]->density, 
                               buffer->size*buffer->size*sizeof(TerrainObjectDensity));
                    }
                    else
                        assert("Terrain object buffer is missing!" == 0);
				}
				break;	
			}
		}
	}
}

// HeightMap

HeightMap *heightMapCreate(ZoneMapLayer *layer, int map_local_pos_x, int map_local_pos_z, HeightMapAtlasData *data)
{
	HeightMap *height_map;
	MP_CREATE(HeightMap, 128);
	height_map = MP_ALLOC(HeightMap);
    height_map->magic = HEIGHTMAP_MAGIC_NUMBER;
	height_map->level_of_detail = default_heightmap_lod;
	memset(height_map->material_ids, UNDEFINED_MAT, NUM_MATERIALS);
	height_map->material_ids[0] = 0;
	height_map->material_count = 1;
	setVec2(height_map->map_local_pos, map_local_pos_x, map_local_pos_z);
	height_map->zone_map_layer = layer;

	if (data)
		height_map->data = data;
	else
		height_map->data = terrainAlloc(1, sizeof(HeightMapAtlasData));

	setVec3(height_map->data->offset, 
		height_map->map_local_pos[0] * GRID_BLOCK_SIZE, 0, height_map->map_local_pos[1] * GRID_BLOCK_SIZE);
	height_map->data->region = layerGetWorldRegion(layer);

	return height_map;
}

HeightMap *heightMapCreateDefault(ZoneMapLayer *layer, int lod, int map_local_pos_x, int map_local_pos_z)
{
	HeightMap *height_map = heightMapCreate(layer, map_local_pos_x, map_local_pos_z, NULL);
	height_map->size = GRID_LOD(lod);

	//printf("Creating default heightmap, lod %d\n", lod);

	height_map->loaded_level_of_detail = lod;
	height_map->level_of_detail = lod;

	heightMapCreateBuffer(height_map, TERRAIN_BUFFER_HEIGHT, lod);
	heightMapCreateBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, lod);
	heightMapCreateBuffer(height_map, TERRAIN_BUFFER_MATERIAL, lod);
    heightMapCreateBuffer(height_map, TERRAIN_BUFFER_COLOR, lod-2);
	heightMapCreateBuffer(height_map, TERRAIN_BUFFER_OBJECTS, lod);

	memset(height_map->material_ids, UNDEFINED_MAT, NUM_MATERIALS);
	height_map->material_ids[0] = 0;
	height_map->material_count = 1;

	height_map->height_time = 1;
    height_map->texture_time = 1;
    height_map->visualization_time = 1;

	return height_map;
}

void heightMapDestroy(HeightMap *height_map)
{
    int i;

	if (height_map->data)
	{
		heightMapAtlasDataFree(height_map->data);
		height_map->data = NULL;
	}

    for (i = 0; i < TERRAIN_BUFFER_NUM_TYPES; i++)
	{
        if (height_map->buffer_list[i])
            DestroyTerrainBuffer(height_map->buffer_list[i]);
	}

    for (i = 0; i <= HEIGHTMAP_MAX_LOD; i++)
    {
        if (height_map->color_buffer_list[i])
            DestroyTerrainBuffer(height_map->color_buffer_list[i]);
        if (height_map->normal_buffer_list[i])
            DestroyTerrainBuffer(height_map->normal_buffer_list[i]);
    }

	eaDestroyEx(&height_map->object_instances, NULL);

	freeTerrainMesh(height_map->terrain_mesh);
	height_map->terrain_mesh = NULL;

    height_map->magic = 0x6A6A6A6A;
	// TomY TODO: More to do here!
	MP_FREE(HeightMap, height_map);
}

void heightMapAtlasDataFree(HeightMapAtlasData *atlas_data)
{
	if (!atlas_data)
		return;

	heightMapDestroyCollision(atlas_data);

	if (atlas_data->client_data.light_cache)
	{
		wl_state.free_dyn_light_cache_func(atlas_data->client_data.light_cache);
		atlas_data->client_data.light_cache = NULL;
	}

	if (atlas_data->client_data.gfx_data && atlas_data->client_data.gfx_free_func)
	{
		atlas_data->client_data.gfx_free_func(atlas_data->client_data.gfx_data);
		atlas_data->client_data.gfx_data = NULL;
	}

	eaDestroyEx(&atlas_data->client_data.model_materials, NULL);
	eaDestroy(&atlas_data->client_data.model_detail_material_names);

	tempModelFree(&atlas_data->client_data.draw_model);
	tempModelFree(&atlas_data->client_data.draw_model_cluster);
	tempModelFree(&atlas_data->client_data.static_vertex_light_model);
	tempModelFree(&atlas_data->client_data.occlusion_model);
	tempModelFree(&atlas_data->collision_model);

	if (atlas_data->client_data.color_texture.data)
	{
		memrefDecrement(atlas_data->client_data.color_texture.data);
		atlas_data->client_data.color_texture.data = NULL;
	}

	free(atlas_data);
}

ZoneMapLayer *heightMapGetLayer(HeightMap *height_map)
{
	if (!height_map) 
		return NULL;
	return height_map->zone_map_layer;
}

void heightMapSetLayer(HeightMap *height_map, ZoneMapLayer *layer)
{
    height_map->zone_map_layer = layer;
    height_map->unsaved = 1;
}

bool heightMapGetMapLocalPos(HeightMap *height_map, IVec2 map_local_pos)
{
	if (!height_map) 
		return false;
	copyVec2(height_map->map_local_pos, map_local_pos);
	return true;
}

/********************************
   Getters / Setters
 ********************************/

void heightMapGetBounds(HeightMap *height_map, Vec3 min, Vec3 max)
{
	addVec3(height_map->bounds.local_min, height_map->bounds.offset, min);
	addVec3(height_map->bounds.local_max, height_map->bounds.offset, max);
}

F32 heightMapGetSoilDepth(HeightMap *height_map, U32 x, U32 z)
{
	TerrainBuffer *buffer;
	F32 depth = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, height_map->loaded_level_of_detail);
	if (buffer) 
		depth = buffer->data_f32[z*height_map->size + x];

	return depth;
}

F32 heightMapGetHeight(HeightMap *height_map, U32 x, U32 z)
{
	TerrainBuffer *buffer;
	F32 height = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
	if (buffer)
        height = buffer->data_f32[z*height_map->size + x];

	return height;
}

F32 heightMapGetSelection(HeightMap *height_map, U32 x, U32 z)
{
	TerrainBuffer *buffer;
	F32 sel = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SELECTION, height_map->loaded_level_of_detail);
	if (buffer)
        sel = buffer->data_f32[z*height_map->size + x];

	return sel;
}

U8 heightMapGetAttribute(HeightMap *height_map, U32 x, U32 z)
{
	TerrainBuffer *buffer;
	U8 attr = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_ATTR, height_map->loaded_level_of_detail);
	if (buffer)
		attr = buffer->data_byte[z*height_map->size + x];

	return attr;
}

F32 heightMapGetShadow(HeightMap *height_map, U32 x, U32 z)
{
	TerrainBuffer *buffer;
	F32 sel = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SHADOW, height_map->loaded_level_of_detail);
	if (buffer)
        sel = buffer->data_byte[z*height_map->size + x]*(1.f/255.f);

	return sel;
}

F32 heightMapGetMaterialWeight(HeightMap *height_map, U32 x, U32 z, U8 local_mat)
{
	int i;
	TerrainBuffer *buffer;
	F32 height = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);
	if (buffer)
	{
		for (i = 0; i < NUM_MATERIALS; i++)
		{
			if(height_map->material_ids[i] == local_mat)
				return buffer->data_material[z*height_map->size + x].weights[i];
		}
	}
	return 0.0f;
}

TerrainObjectDensity heightMapGetObjectDensity(HeightMap *height_map, U32 x, U32 z, U8 object_type)
{
	int i;
	TerrainBuffer *buffer;
	F32 density;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
	if (buffer)
	{
		for(i=0; i<eaSize(&buffer->data_objects); i++)
		{
			if(buffer->data_objects[i]->object_type == object_type)
			{
				density = buffer->data_objects[i]->density[x + z*height_map->size];	
				return density; 
			}
		}
	}

	return 0;
}

F32 heightMapGetHeightLOD(HeightMap *height_map, U32 x, U32 z)
{
	TerrainBuffer *buffer;
	F32 height = 0.f;
	U32 lod = height_map->level_of_detail-height_map->loaded_level_of_detail;
	U32 sx = x << lod;
	U32 sz = z << lod;

	assert(sx<height_map->size && sz<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
	if (buffer) height = buffer->data_f32[sz*height_map->size + sx];

	return height;
}

void heightMapSetHeight(HeightMap *height_map, U32 x, U32 z, F32 newvalue)
{
	TerrainBuffer *buffer;
	F32 height = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
	if (buffer)
	{
		buffer->data_f32[z*height_map->size + x] = newvalue;
		heightMapModify(height_map);
		heightMapUpdateGeometry(height_map);
		heightMapUpdateVisualization(height_map);
        height_map->buffer_touched[TERRAIN_BUFFER_HEIGHT] = true;
	}
}

void heightMapSetSelection(HeightMap *height_map, U32 x, U32 z, F32 newvalue)
{
	TerrainBuffer *buffer;
	F32 sel = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SELECTION, height_map->loaded_level_of_detail);
    if (!buffer)
        buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_SELECTION, height_map->loaded_level_of_detail);
	if (buffer)
	{
		buffer->data_f32[z*height_map->size + x] = newvalue;
        heightMapUpdateVisualization(height_map);
        height_map->buffer_touched[TERRAIN_BUFFER_SELECTION] = true;
	}
}

void heightMapSetAttribute(HeightMap *height_map, U32 x, U32 z, U8 newvalue)
{
	TerrainBuffer *buffer;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_ATTR, height_map->loaded_level_of_detail);
	if (buffer)
	{
		buffer->data_byte[z*height_map->size + x] = newvalue;
        heightMapUpdateVisualization(height_map);
		height_map->buffer_touched[TERRAIN_BUFFER_ATTR] = true;
	}
}

void heightMapSetSoilDepth(HeightMap *height_map, U32 x, U32 z, F32 newvalue)
{
	TerrainBuffer *buffer;
	F32 height = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, height_map->loaded_level_of_detail);
	if(!buffer)
		buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, height_map->loaded_level_of_detail);

	buffer->data_f32[z*height_map->size + x] = newvalue;
    heightMapUpdateVisualization(height_map);
    height_map->buffer_touched[TERRAIN_BUFFER_SOIL_DEPTH] = true;
}

void heightMapSetObjectDensity(HeightMap *height_map, U32 x, U32 z, TerrainObjectDensity newvalue, U8 object_type)
{
	int i;
	TerrainBuffer *buffer;
	TerrainObjectBuffer *sub_buffer;
	F32 height = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
	if(!buffer)
		buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);

	for(i=0; i<eaSize(&buffer->data_objects); i++)
	{
		sub_buffer = buffer->data_objects[i];
		if(sub_buffer->object_type == object_type)
		{
			sub_buffer->density[x + z*height_map->size] = newvalue;
            heightMapUpdateVisualization(height_map);
            height_map->buffer_touched[TERRAIN_BUFFER_OBJECTS] = true;
			return;
		}
	}

	//If no existing buffer was found, make one
	sub_buffer = terrainAlloc(1, sizeof(TerrainObjectBuffer));
	if (sub_buffer)
	{
		sub_buffer->object_type = object_type;
		sub_buffer->density = calloc(buffer->size*buffer->size, sizeof(TerrainObjectDensity));	
		eaPush(&buffer->data_objects, sub_buffer);		

		sub_buffer->density[x + z*height_map->size] = newvalue;
		heightMapUpdateVisualization(height_map);
		height_map->buffer_touched[TERRAIN_BUFFER_OBJECTS] = true;
	}
}

void heightMapSetAllObjectsAndBackup(HeightMap *height_map, U32 x, U32 z, int draw_lod, TerrainObjectDensity newvalue)
{
	int i;
	TerrainBuffer *buffer;
	F32 height = 0;

	assert(x<height_map->size && z<height_map->size);

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
	if(!buffer)
		buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);

	for(i=0; i<eaSize(&buffer->data_objects); i++)
	{
		heightMapMakeBackup(height_map, x, z, draw_lod, TERRAIN_BUFFER_OBJECTS, buffer->data_objects[i]->object_type); 
		buffer->data_objects[i]->density[x + z*height_map->size] = newvalue;
	}
    heightMapUpdateVisualization(height_map);
	height_map->buffer_touched[TERRAIN_BUFFER_OBJECTS] = true;
}

void heightMapGetVertexColorVec3(HeightMap *height_map, U32 x, U32 z, U32 lod, U8Vec3 *ret)
{
	TerrainBuffer *buffer;

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
	if (!buffer) {
		setVec3same((*ret), 255);
		return;
	}

	assert(x<(U32)buffer->size && z<(U32)buffer->size);
	(*ret)[0] = buffer->data_u8vec3[z*buffer->size + x][2];
	(*ret)[1] = buffer->data_u8vec3[z*buffer->size + x][1];
	(*ret)[2] = buffer->data_u8vec3[z*buffer->size + x][0];

	return;
}

void	heightMapSetVertexColorVec3(HeightMap *height_map, U32 x, U32 z, U32 lod, U8Vec3 newcolor)
{
	TerrainBuffer *buffer;

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
	if (!buffer)
	{
		int size = GRID_LOD(lod);
		buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
		ClearTerrainBuffer(height_map->zone_map_layer, buffer);
	}

	assert(x<(U32)buffer->size && z<(U32)buffer->size);
	buffer->data_u8vec3[z*buffer->size + x][0] = newcolor[2];
	buffer->data_u8vec3[z*buffer->size + x][1] = newcolor[1];
	buffer->data_u8vec3[z*buffer->size + x][2] = newcolor[0];

	heightMapUpdateTextures(height_map);    
    height_map->buffer_touched[TERRAIN_BUFFER_COLOR] = true;
}

Color heightMapGetVertexColor(HeightMap *height_map, U32 x, U32 z, U32 lod)
{
	TerrainBuffer *buffer;
	Color ret;


	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
	if (!buffer) {
		ret.r = ret.g = ret.b = ret.a = 255;
		return ret;
	}

	assert(x<(U32)buffer->size && z<(U32)buffer->size);
	ret.r = buffer->data_u8vec3[z*buffer->size + x][2];
	ret.g = buffer->data_u8vec3[z*buffer->size + x][1];
	ret.b = buffer->data_u8vec3[z*buffer->size + x][0];
	ret.a = 255;

	return ret;
}

void heightMapSetVertexColor(HeightMap *height_map, U32 x, U32 z, U32 lod, Color newcolor)
{
	TerrainBuffer *buffer;

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
	if (!buffer) 
	{
		int size = GRID_LOD(lod);
		buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
		ClearTerrainBuffer(height_map->zone_map_layer, buffer);
	}

	assert(x<(U32)buffer->size && z<(U32)buffer->size);
	buffer->data_u8vec3[z*buffer->size + x][2] = newcolor.r;
	buffer->data_u8vec3[z*buffer->size + x][1] = newcolor.g;
	buffer->data_u8vec3[z*buffer->size + x][0] = newcolor.b;

    heightMapUpdateTextures(height_map);
    height_map->buffer_touched[TERRAIN_BUFFER_COLOR] = true;
}

U8 heightMapGetAlpha(HeightMap *height_map, U32 x, U32 z, U32 lod)
{
	TerrainBuffer *buffer;
	int lod_diff;
	U32 sx, sz;

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_ALPHA, height_map->loaded_level_of_detail);
	if (buffer)
	{
		lod_diff = lod-height_map->loaded_level_of_detail;
		sx = (lod_diff >= 0) ? x << lod_diff : x >> -lod_diff;
		sz = (lod_diff >= 0) ? z << lod_diff : z >> -lod_diff;

		assert(sx<(U32)buffer->size && sz<(U32)buffer->size);
		return buffer->data_byte[sz*buffer->size + sx];
	}

	if (layerGetMode(height_map->zone_map_layer) < LAYER_MODE_TERRAIN)
	{
		buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
		if (buffer)
		{
			return (*((U32*)&buffer->data_f32[z*height_map->size + x]) & 1) * 255;
		}
	}

	return 255;
}

void heightMapSetAlpha(HeightMap *height_map, U32 x, U32 z, U32 lod, U8 alpha)
{
	TerrainBuffer *buffer;
	U32 lod_diff = lod-height_map->loaded_level_of_detail;
	U32 sx = x << lod_diff;
	U32 sz = z << lod_diff;

	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_ALPHA, height_map->loaded_level_of_detail);
	if (!buffer) 
	{
		if (alpha == 255)
			return;
		buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_ALPHA, height_map->loaded_level_of_detail);
	}

	assert(sx<(U32)buffer->size && sz<(U32)buffer->size);
	buffer->data_byte[sz*buffer->size + sx] = alpha;

    heightMapUpdateGeometry(height_map);
    height_map->buffer_touched[TERRAIN_BUFFER_ALPHA] = true;
}

int heightMapAddMaterial(HeightMap *height_map, U8 id)
{
    int idx;
    assert(height_map->material_count < NUM_MATERIALS);
    idx = height_map->material_count++;
    height_map->material_ids[idx] = id;
    return idx;
}

void heightMapSetMaterialWeights(HeightMap *height_map, U32 x, U32 z, TerrainMaterialWeight mat_weights)
{
	TerrainBuffer *buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);

	if (!buffer)
	{
		buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);
		ClearTerrainBuffer(height_map->zone_map_layer, buffer);
	}

	assert(x >= 0 && z >= 0 && x<height_map->size && z<height_map->size);
	memcpy(&buffer->data_material[z*height_map->size + x], &mat_weights, sizeof(TerrainMaterialWeight));
    heightMapUpdateGeometry(height_map);
    height_map->buffer_touched[TERRAIN_BUFFER_MATERIAL] = true;
}

void heightMapSetMaterialIDs(HeightMap *height_map, U8 material_ids[NUM_MATERIALS], int num_materials)
{
    memcpy(height_map->material_ids, material_ids, NUM_MATERIALS);
    height_map->material_count = num_materials;
    heightMapUpdateGeometry(height_map);
    height_map->buffer_touched[TERRAIN_BUFFER_MATERIAL] = true;
}

void heightMapGetMaterialIDs(HeightMap *height_map, U8 material_ids[NUM_MATERIALS], int *num_materials)
{
    int i;
    *num_materials = height_map->material_count;
    for (i = 0; i < NUM_MATERIALS; i++)
        material_ids[i] = height_map->material_ids[i];
}

TerrainMaterialWeight *heightMapGetMaterialWeights(HeightMap *height_map, U32 x, U32 z, const char *material_names[NUM_MATERIALS])
{
	int i;
	TerrainBuffer *buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);

	if (!buffer) 
	{
		if(material_names)
		{
			for(i=0; i < NUM_MATERIALS; i++)
			{
				material_names[i] = NULL;
			}
		}
		return NULL;
	}

	assert(x<height_map->size && z<height_map->size);

	if(material_names)
	{
		for(i=0; i < NUM_MATERIALS; i++)
		{
			material_names[i] = terrainGetLayerMaterial(height_map->zone_map_layer, height_map->material_ids[i]);
		}
	}

	return &buffer->data_material[z*height_map->size + x];
}


void heightMapGetNormal(HeightMap *height_map, U32 x, U32 z, U32 lod, U8 normal[3])
{
	TerrainBuffer *buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_NORMAL, lod);
	if (!buffer)
	{
		normal[0] = normal[2] = 128;
		normal[1] = 255;
		return;
	}
	assert(x<(U32)buffer->size && z<(U32)buffer->size);
	copyVec3(buffer->data_normal[z*buffer->size + x], normal);
}

U32 heightMapGetSize(HeightMap *height_map) // Size of this square heightmap
{
	return height_map->size;
}

U32 heightMapGetLoadedLOD(HeightMap *height_map)
{
	return height_map->loaded_level_of_detail;
}

void heightMapSetLevelOfDetail(HeightMap *height_map, U32 level_of_detail)
{
	height_map->level_of_detail = level_of_detail;
	height_map->height_time++;
    height_map->texture_time++;
}

void heightMapModify(HeightMap *height_map)
{
	if (wlIsServer())
	{

		height_map->mod_time = worldIncModTime();
	}
	else
		height_map->mod_time = ++height_map_system_mod_time;
	height_map->unsaved = 1;
}

void heightMapUpdate(HeightMap *height_map)
{
	height_map->unsaved = 1;
	height_map->height_time++;
    height_map->texture_time++;
}

void heightMapUpdateGeometry(HeightMap *height_map)
{
	height_map->unsaved = 1;
	height_map->height_time++;
}

void heightMapUpdateTextures(HeightMap *height_map)
{
	height_map->unsaved = 1;
	height_map->texture_time++;
}

void heightMapUpdateVisualization(HeightMap *height_map)
{
	height_map->unsaved = 1;
	height_map->visualization_time++;
}

void heightMapMarkUnsaved(HeightMapTracker *tracker, void *unused)
{
	if (!tracker->height_map)
		return;
	tracker->height_map->unsaved = 1;
}

bool heightMapIsUnsaved(HeightMap *height_map)
{
	return (height_map->unsaved != 0);
}

bool heightMapWasTouched(HeightMap *height_map, TerrainBufferType type)
{
    return height_map->buffer_touched[type];
}

void heightMapClearTouchedBits(HeightMap *height_map)
{
    int i;
    for (i = 0; i < TERRAIN_BUFFER_NUM_TYPES; i++)
        height_map->buffer_touched[i] = false;
}

/********************************
   Utility Functions
 ********************************/

static void heightMapCalcNormal(HeightMap *height_maps[3][3], int x, int y, U8 *normal_out, F32 *shadow_out, TerrainBuffer *buffers[3][3])
{
	Vec3 norm;
	static int dx[] = {0, 0, 0, 1, 2, 2, 2, 1 };
	static int dy[] = {0, 1, 2, 2, 2, 1, 0, 0 };
    static F32 dist[] = { 1.414213f, 1.f, 1.414213f, 1.f, 1.414213f, 1.f, 1.414213f, 1.f }; 
	Vec3 vec_x, vec_y;
	F32 spacing = (1 << height_maps[1][1]->loaded_level_of_detail);
	F32 samples[8];
	F32 h_delta_x, h_delta_y;
	F32 height;
	int i;
	int idx_x[] = { 1, 1, 1 };
	int idx_y[] = { 1, 1, 1 };
	int pos_x[] = { x-1, x, x+1 };
	int pos_y[] = { y-1, y, y+1 };
	int size = heightMapGetSize(height_maps[1][1]);
	assert(buffers[1][1]);

	if (pos_x[0] < 0) { idx_x[0] = 0; pos_x[0] += size-1; }
	if (pos_y[0] < 0) { idx_y[0] = 0; pos_y[0] += size-1; }
	if (pos_x[2] >= size) { idx_x[2] = 2; pos_x[2] -= size-1; }
	if (pos_y[2] >= size) { idx_y[2] = 2; pos_y[2] -= size-1; }

	for (i=0; i<8; i++)
	{
		int ix = idx_x[dx[i]];
		int iy = idx_y[dy[i]];

		if (!buffers[ix][iy])
			samples[i] = buffers[1][1]->data_f32[CLAMP(y+dy[i]-1, 0, size-1)*buffers[1][1]->size + CLAMP(x+dx[i]-1, 0, size-1)];
		else
		{
			int px = pos_x[dx[i]];
			int py = pos_y[dy[i]];
			samples[i] = buffers[ix][iy]->data_f32[py*buffers[ix][iy]->size + px];
		}
	}
    height = buffers[1][1]->data_f32[y*buffers[1][1]->size + x];

    // Compute normal
	h_delta_x = (samples[4] + samples[5] + samples[6]) - (samples[0] + samples[1] + samples[2]);
	h_delta_y = (samples[2] + samples[3] + samples[4]) - (samples[0] + samples[6] + samples[7]);

	setVec3(vec_x, 0.f, h_delta_x, 6.f*spacing);
	setVec3(vec_y, 6.f*spacing, h_delta_y, 0.f);

	crossVec3(vec_x, vec_y, norm);
	normalVec3(norm);

	normal_out[0] = norm[0]*127 + 128;
	normal_out[1] = norm[1]*127 + 128;
	normal_out[2] = norm[2]*-127 + 128;

    // Compute shadow coefficient
    *shadow_out = 0.f;
    for (i = 0; i < 8; i++)
    {
        F32 delta = samples[i]-height;
        F32 angle = 1.5708f;
        if (delta > 0)
            angle = atan2(dist[i], delta);
        else if (delta < 0)
            angle = 1.5708f + atan2(-delta, dist[i]);
        (*shadow_out) += angle * 0.636618f * 0.125f;
    }
}

void heightMapUpdateNormals(HeightMap *height_maps[3][3])
{
	int x, y;
	int lod = heightMapGetLoadedLOD(height_maps[1][1]);
	int lod_size = GRID_LOD(lod), normal_size = lod_size;
	int stride = 1;
	U8 *downsample_array;
	TerrainBuffer *height_buffers[3][3];
	TerrainBuffer *normal_buffer = heightMapGetBuffer(height_maps[1][1], TERRAIN_BUFFER_NORMAL, lod);
    TerrainBuffer *shadow_buffer = heightMapGetBuffer(height_maps[1][1], TERRAIN_BUFFER_SHADOW, lod);

	SET_FP_CONTROL_WORD_DEFAULT;

	if (!normal_buffer)
		normal_buffer = heightMapCreateBuffer(height_maps[1][1], TERRAIN_BUFFER_NORMAL, lod);
    if (!shadow_buffer)
	{
		heightMapDeleteBuffer(height_maps[1][1], TERRAIN_BUFFER_SHADOW, -1);
        shadow_buffer = heightMapCreateBuffer(height_maps[1][1], TERRAIN_BUFFER_SHADOW, lod);
	}

	for (x = 0; x < 3; x++)
		for (y = 0; y < 3; y++)
			height_buffers[x][y] = (height_maps[x][y] != NULL && height_maps[x][y]->loaded_level_of_detail == lod) ? 
				heightMapGetBuffer(height_maps[x][y], TERRAIN_BUFFER_HEIGHT, lod) : NULL;

	downsample_array = ScratchAlloc(lod_size*lod_size*3);
	for (y = 0; y < lod_size; y++)
		for (x = 0; x < lod_size; x++)
		{
			// save normal
			U8 *buf = (U8*)&normal_buffer->data_normal[x+y*lod_size];
            F32 shadow;
			heightMapCalcNormal(height_maps, x, y, buf, &shadow, height_buffers);
			memcpy(&downsample_array[(x + y*lod_size)*3], buf, 3);
            shadow_buffer->data_byte[y*lod_size + x] = (U8)CLAMP(shadow*255, 0, 255);
		}

	for (++lod; lod < MAX_TERRAIN_LODS; lod++)
	{
		lod_size = GRID_LOD(lod);
		stride *= 2;

		normal_buffer = heightMapGetBuffer(height_maps[1][1], TERRAIN_BUFFER_NORMAL, lod);
		if (!normal_buffer)
			normal_buffer = heightMapCreateBuffer(height_maps[1][1], TERRAIN_BUFFER_NORMAL, lod);

		for (y = 0; y < lod_size; y++)
			for (x = 0; x < lod_size; x++)
			{
                int c;
				F32 normal[3];
                F32 accum[] = { 0.f, 0.f, 0.f };
                U32 x2 = x * stride;
                U32 y2 = y * stride;
				U8 *buf = (U8*)&normal_buffer->data_normal[x+y*lod_size];
                if (x == 0 || y == 0 || x == (lod_size-1) || (y == lod_size-1))
                {
                    // 3-point edge case
                    F32 coefficients[] = { 0.2f, 0.6f, 0.2f };
                    U32 d1, d2;
                    if (x == 0 || x == (lod_size-1))
                    {
                        if (x == 0)
                            d1 = x2 + (stride/2);
                        else
                            d1 = x2 - (stride/2);
                        if (y == 0)
                        {
                            // L shape, top
                            d2 = y2 + (stride/2);                            
                            for (c=0; c<3; ++c)
                            {
                                accum[c] += downsample_array[(d1 + y2*normal_size)*3+c] * coefficients[0];
                                accum[c] += downsample_array[(x2 + y2*normal_size)*3+c] * coefficients[1];
                                accum[c] += downsample_array[(x2 + d2*normal_size)*3+c] * coefficients[2];
                            }
                        }
                        else if (y == (lod_size-1))
                        {
                            // L shape, bottom
                            d2 = y2 - (stride/2);
                            for (c=0; c<3; ++c)
                            {
                                accum[c] += downsample_array[(d1 + y2*normal_size)*3+c] * coefficients[0];
                                accum[c] += downsample_array[(x2 + y2*normal_size)*3+c] * coefficients[1];
                                accum[c] += downsample_array[(x2 + d2*normal_size)*3+c] * coefficients[2];
                            }
                        }
                        else
                        {
                            // Vertical
                            d1 = y2 - (stride/2);
                            d2 = y2 + (stride/2);
                            for (c=0; c<3; ++c)
                            {
                                accum[c] += downsample_array[(x2 + d1*normal_size)*3+c] * coefficients[0];
                                accum[c] += downsample_array[(x2 + y2*normal_size)*3+c] * coefficients[1];
                                accum[c] += downsample_array[(x2 + d2*normal_size)*3+c] * coefficients[2];
                            }
                        }
                    }
                    else
                    {
                        // Horizontal
                        d1 = x2 - (stride/2);
                        d2 = x2 + (stride/2);
                        for (c=0; c<3; ++c)
                        {
	                        accum[c] += downsample_array[(d1 + y2*normal_size)*3+c] * coefficients[0];
	                        accum[c] += downsample_array[(x2 + y2*normal_size)*3+c] * coefficients[1];
	                        accum[c] += downsample_array[(d2 + y2*normal_size)*3+c] * coefficients[2];
                        }
                    }
                }
                else
                {
                    // 9-point center case
                    F32 coefficients[] = { 0.05f, 0.1f, 0.05f,
                    					   0.1f,  0.4f, 0.1f,
                    					   0.05f, 0.1f, 0.05f };
                    U32 d1, d2, d3, d4;
                    d1 = x2 - (stride/2);
                    d2 = x2 + (stride/2);
                    d3 = y2 - (stride/2);
                    d4 = y2 + (stride/2);
                    for (c=0; c<3; ++c)
                    {
                        accum[c] += downsample_array[(d1 + d3*normal_size)*3+c] * coefficients[0];
                        accum[c] += downsample_array[(x2 + d3*normal_size)*3+c] * coefficients[1];
                        accum[c] += downsample_array[(d2 + d3*normal_size)*3+c] * coefficients[2];
                        accum[c] += downsample_array[(d1 + y2*normal_size)*3+c] * coefficients[3];
                        accum[c] += downsample_array[(x2 + y2*normal_size)*3+c] * coefficients[4];
                        accum[c] += downsample_array[(d2 + y2*normal_size)*3+c] * coefficients[5];
                        accum[c] += downsample_array[(d1 + d4*normal_size)*3+c] * coefficients[6];
                        accum[c] += downsample_array[(x2 + d4*normal_size)*3+c] * coefficients[7];
                        accum[c] += downsample_array[(d2 + d4*normal_size)*3+c] * coefficients[8];
                    }
                }
                for (c=0; c<3; ++c)
                    normal[c] = (accum[c] - 128.f)*0.007874015748f;
				normalVec3(normal);
				for (c=0; c<3; c++)
				{
					U8 val = (normal[c]*127) + 128;
					downsample_array[(x2 + y2*normal_size)*3+c] = val;
					*(buf++) = val;
                }
			}
	}
	ScratchFree(downsample_array);
}

void heightMapGetNeighbors(HeightMap *height_maps[3][3], HeightMap **map_cache)
{
    int j;
    height_maps[0][0] = height_maps[1][0] = height_maps[2][0] = NULL;
    height_maps[0][1] = height_maps[2][1] = NULL;
    height_maps[0][2] = height_maps[1][2] = height_maps[2][2] = NULL;
    for (j = 0; j < eaSize(&map_cache); j++)
    {
        if (map_cache[j]->map_local_pos[0] == height_maps[1][1]->map_local_pos[0]-1 && 
            map_cache[j]->map_local_pos[1] == height_maps[1][1]->map_local_pos[1]-1)
            height_maps[0][0] = map_cache[j];
        if (map_cache[j]->map_local_pos[0] == height_maps[1][1]->map_local_pos[0] && 
            map_cache[j]->map_local_pos[1] == height_maps[1][1]->map_local_pos[1]-1)
            height_maps[1][0] = map_cache[j];
        if (map_cache[j]->map_local_pos[0] == height_maps[1][1]->map_local_pos[0]+1 && 
            map_cache[j]->map_local_pos[1] == height_maps[1][1]->map_local_pos[1]-1)
            height_maps[2][0] = map_cache[j];
        if (map_cache[j]->map_local_pos[0] == height_maps[1][1]->map_local_pos[0]-1 && 
            map_cache[j]->map_local_pos[1] == height_maps[1][1]->map_local_pos[1])
            height_maps[0][1] = map_cache[j];
        if (map_cache[j]->map_local_pos[0] == height_maps[1][1]->map_local_pos[0]+1 && 
            map_cache[j]->map_local_pos[1] == height_maps[1][1]->map_local_pos[1])
            height_maps[2][1] = map_cache[j];
        if (map_cache[j]->map_local_pos[0] == height_maps[1][1]->map_local_pos[0]-1 && 
            map_cache[j]->map_local_pos[1] == height_maps[1][1]->map_local_pos[1]+1)
            height_maps[0][2] = map_cache[j];
        if (map_cache[j]->map_local_pos[0] == height_maps[1][1]->map_local_pos[0] && 
            map_cache[j]->map_local_pos[1] == height_maps[1][1]->map_local_pos[1]+1)
            height_maps[1][2] = map_cache[j];
        if (map_cache[j]->map_local_pos[0] == height_maps[1][1]->map_local_pos[0]+1 && 
            map_cache[j]->map_local_pos[1] == height_maps[1][1]->map_local_pos[1]+1)
            height_maps[2][2] = map_cache[j];
    }
}

void heightMapUpdateColors(HeightMap *height_maps[3][3], int lod)
{
	U32 x, z, c, old_size, size, avg_count;
	int px, pz;
	F32 avg;
	TerrainBuffer *color_buffer, *next_color_buffer;
	HeightMap *height_map = height_maps[1][1];

	color_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
	if (!color_buffer)
		return;

	size = GRID_LOD(lod);

	next_color_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod+1);
	if (!next_color_buffer)
	{
		next_color_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_COLOR, lod+1);
	}

	old_size = size;
	size = ((size - 1) >> 1) + 1;

	//For each point average the points surrounding and self to find new value
	for (z=0; z<size; z++)
	{
		for (x=0; x<size; x++)
		{
			for(c=0; c<3; c++)
			{
				avg = 0;
				avg_count = 0;			
				for(pz = ((int)z<<1)-1; pz <= ((int)z<<1)+1; pz++)
					for(px = ((int)x<<1)-1; px <= ((int)x<<1)+1; px++)
					{
						//If we are inside our base map
						if(px >= 0 && px < (int)old_size && pz >= 0 && pz < (int)old_size)
						{
							avg += (F32)color_buffer->data_u8vec3[px+pz*color_buffer->size][c];
							avg_count++;
						}	
						//Otherwise if we are looking for a color that is on another map then 
						//we have to find the new map and the position on that map
						else
						{
							int pos[2];
							HeightMap *touching_height_map;
							//First find correct map
							//If < 0 then 2, if > size then 0, else 1
							touching_height_map = height_maps	[(px < 0 ? 0 : (px >= (int)old_size ? 2 : 1))]
																[(pz < 0 ? 0 : (pz >= (int)old_size ? 2 : 1))];
							//Then find the position on that map
							//If < 0 then size+x (with x being negitive), if > size then x-size, else just use x
							// +/- 1 is to compensate for extra bits that surround the right and top of each map
							pos[0] = px < 0 ? old_size+px-1 : (px >= (int)old_size ? px-old_size+1 : px);
							pos[1] = pz < 0 ? old_size+pz-1 : (pz >= (int)old_size ? pz-old_size+1 : pz);
							if (touching_height_map)
							{
								TerrainBuffer *touching_color_buffer = heightMapGetBuffer(touching_height_map, TERRAIN_BUFFER_COLOR, lod);
								if (touching_color_buffer)
								{
									avg += (F32)touching_color_buffer->data_u8vec3[pos[0]+pos[1]*touching_color_buffer->size][c];
									avg_count++;										
								}
							}
						}
					}
				avg /= (F32)avg_count;
				next_color_buffer->data_u8vec3[x+z*next_color_buffer->size][c] = avg + 0.5;
			}
		}
	}
    height_map->texture_time++;
}

/********************************
   Loading SOURCE format
 ********************************/

TerrainBuffer *heightMapTiffColorLoadGetBuffer(ZoneMapLayer *layer, TerrainBlockRange *range, U32 x, U32 y, int lod, HeightMap ***cache)
{
	U32 i;
	IVec2 local_grid_pos;
	HeightMap *height_map=NULL;

    if (range)
    {
        local_grid_pos[0] = x+range->range.min_block[0];
        local_grid_pos[1] = y+range->range.min_block[2];
    }
    else
    {
        local_grid_pos[0] = x;
        local_grid_pos[1] = y;
    }

	if (cache)
	{
		for (i = 0; i < eaUSize(cache); i++)
		{
			if ((*cache)[i]->map_local_pos[0] == local_grid_pos[0] &&
				(*cache)[i]->map_local_pos[1] == local_grid_pos[1])
			{
				height_map = (*cache)[i];
				break;
			}
		}
		if (!height_map)
			return NULL;	
	}
	else
	{
        return NULL;
	}
	return heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
}

void importTiffColorLoadLine(TerrainBuffer **color_buffers, U8 *strip, int strip_width, int y, int lod, bool swizzle)
{
    int x, idx = 0;
    int grid_size = GRID_LOD(lod);
    for(x=0; x < strip_width; x++)
    {
        U32 buff_y = y;
        U32 buff_x = x % (grid_size-1);
        assert(	buff_x < (U32)grid_size &&
                buff_y < (U32)grid_size );
        if(swizzle && x != 0)
        {
            strip[x*3 + 0] += strip[(x-1)*3 + 0];
            strip[x*3 + 1] += strip[(x-1)*3 + 1];
            strip[x*3 + 2] += strip[(x-1)*3 + 2];
        }
        if (buff_x == 0 && x > 0)
        {
            if (color_buffers[idx])
            {
                color_buffers[idx]->data_u8vec3[grid_size-1 + buff_y*color_buffers[idx]->size][2] = strip[x*3 + 0];
                color_buffers[idx]->data_u8vec3[grid_size-1 + buff_y*color_buffers[idx]->size][1] = strip[x*3 + 1];
                color_buffers[idx]->data_u8vec3[grid_size-1 + buff_y*color_buffers[idx]->size][0] = strip[x*3 + 2];
            }
            idx++;
        }
        if (color_buffers[idx])
        {
            color_buffers[idx]->data_u8vec3[buff_x + buff_y*color_buffers[idx]->size][2] = strip[x*3 + 0];
            color_buffers[idx]->data_u8vec3[buff_x + buff_y*color_buffers[idx]->size][1] = strip[x*3 + 1];
            color_buffers[idx]->data_u8vec3[buff_x + buff_y*color_buffers[idx]->size][0] = strip[x*3 + 2];
        }
    }
   
}

bool heightMapTiffColorLoad(ZoneMapLayer *layer, TerrainFile *tfile, TerrainBlockRange *range, int lod, HeightMap ***cache)
{
	U32 i, map_x, y;
	U8 *strip, *strip_ptr;
	U32 idf_offset=0;
	U32 grid_size = GRID_LOD(lod);
	TiffIFD imageFileDir = {0};
	U32 remaining_lines;
	U32 map_y=0;
	U32 range_width = (range->range.max_block[0]+1-range->range.min_block[0]);
	U32 range_height = (range->range.max_block[2]+1-range->range.min_block[2]);
	TerrainBuffer **color_buffers;

	if(!tiffReadHeader(tfile->file, &idf_offset))
		return false;		

	if(!tiffReadIFD(tfile->file, &imageFileDir, idf_offset))
	{
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(imageFileDir.bytesPerChannel != 1)
	{
		ErrorFilenamef(tfile->file->nameptr, "ERROR - Bad Tiff Format: Invalid Bits Per Sample");
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(imageFileDir.horizontalDifferencingType != TIFF_DIFF_HORIZONTAL)
	{
		ErrorFilenamef(tfile->file->nameptr, "ERROR - Bad Tiff Format: Not using horizontal differencing");
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(!imageFileDir.lzwCompressed)
	{
		ErrorFilenamef(tfile->file->nameptr, "ERROR - Bad Tiff Format: Not using LZW compression");
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(	imageFileDir.width  != range_width *(grid_size-1) + 1	||
		imageFileDir.height != range_height*(grid_size-1) + 1	)
	{
		ErrorFilenamef(tfile->file->nameptr, "ERROR - Corrupted Terrain Tiff File: \nInvalid image size for color data.\nExpected: width=%d, height=%d", range_width *(grid_size-1) + 1 , range_height*(grid_size-1) + 1);
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

    color_buffers = calloc(1, sizeof(void*)*(range_width+1));

	strip = calloc(imageFileDir.width * imageFileDir.rows_per_strip,  sizeof(U8)*3);
    i = 0;
    remaining_lines = 0;
    for (y = 0; y < imageFileDir.height; y++)
    {
        if (remaining_lines == 0)
        {
            remaining_lines = imageFileDir.height-y;
            if (remaining_lines > imageFileDir.rows_per_strip)
                remaining_lines = imageFileDir.rows_per_strip;

            if (i >= imageFileDir.strip_count ||
                !tiffReadStrip(tfile->file, &imageFileDir, strip, remaining_lines, i))
            {
				free(color_buffers);
                free(strip);
                free(imageFileDir.strip_offsets);
                free(imageFileDir.strip_byte_counts);
                return false;
            }
            strip_ptr = strip;
            i++;
        }

        if ((y % (grid_size-1)) == 0)
        {
            // Last line copied into old tiles
            importTiffColorLoadLine(color_buffers, strip_ptr, imageFileDir.width, 0, lod, true);
            for (map_x = 0; map_x < range_width; map_x++)
            {
				if(map_y < range_height)
                {
					if ((color_buffers[map_x] = heightMapTiffColorLoadGetBuffer(layer, range, map_x, (range_height-1) - map_y, lod, cache)) != NULL)
                    {		
                        /*Errorf("ERROR: Missing color buffer on load. %s", tfile->file->nameptr);
                        free(strip);
                        free(imageFileDir.strip_offsets);
                        free(imageFileDir.strip_byte_counts);
                        return false;*/
                        assert(color_buffers[map_x]->size == grid_size);
                    }
                }
                else
                    color_buffers[map_x] = NULL;
            }
            importTiffColorLoadLine(color_buffers, strip_ptr, imageFileDir.width, (grid_size-1), lod, false);
            map_y++;
        }
        else
        {
            importTiffColorLoadLine(color_buffers, strip_ptr, imageFileDir.width, (grid_size-1)-(y%(grid_size-1)), lod, true);
        }
        
        remaining_lines--;
        strip_ptr += imageFileDir.width*3;
    }
    free(color_buffers);
	free(strip);
	free(imageFileDir.strip_offsets);
	free(imageFileDir.strip_byte_counts);
	return true;
}

TerrainBuffer *importTiffColorLoadGetBuffer(StashTable heightmaps, U32 x, U32 y, int lod)
{
	IVec2 local_grid_pos;
	HeightMap *height_map=NULL;
    local_grid_pos[0] = x;
    local_grid_pos[1] = y;
    if(!stashIntFindPointer(heightmaps, getTerrainGridPosKey(local_grid_pos), &height_map))
        return NULL;
    return heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
}

bool terrainImportColorTiff(StashTable heightmaps, TerrainExportInfo *info, FILE *file_color, int lod)
{
	int i, l;
    U32 map_x, y;
	U8 *strip, *strip_ptr;
	U32 idf_offset=0;
	U32 grid_size = GRID_LOD(lod);
	TiffIFD imageFileDir = {0};
	U32 remaining_lines;
	U32 map_y=0;
	U32 range_width;
	U32 range_height;
    IVec2 min_pos, max_pos;
	TerrainBuffer **color_buffers;

    if (eaSize(&info->blocks) < 1)
        return false;
    
	if(!tiffReadHeader(file_color, &idf_offset))
		return false;

    copyVec2(info->blocks[0]->min, min_pos);
    copyVec2(info->blocks[0]->max, max_pos);
    for (i = 1; i < eaSize(&info->blocks); i++)
    {
        if (info->blocks[i]->min[0] < min_pos[0])
            min_pos[0] = info->blocks[i]->min[0];
        if (info->blocks[i]->min[1] < min_pos[1])
            min_pos[1] = info->blocks[i]->min[1];
        if (info->blocks[i]->max[0] > max_pos[0])
            max_pos[0] = info->blocks[i]->max[0];
        if (info->blocks[i]->max[1] > max_pos[1])
            max_pos[1] = info->blocks[i]->max[1];
    }
    range_width = max_pos[0]+1-min_pos[0];
    range_height = max_pos[1]+1-min_pos[1];

	if(!tiffReadIFD(file_color, &imageFileDir, idf_offset))
	{
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(imageFileDir.bytesPerChannel != 1)
	{
		ErrorFilenamef(file_color->nameptr, "ERROR - Bad Tiff Format: Invalid Bits Per Sample");
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(imageFileDir.horizontalDifferencingType != TIFF_DIFF_HORIZONTAL)
	{
		ErrorFilenamef(file_color->nameptr, "ERROR - Bad Tiff Format: Not using horizontal differencing");
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(!imageFileDir.lzwCompressed)
	{
		ErrorFilenamef(file_color->nameptr, "ERROR - Bad Tiff Format: Not using LZW compression");
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(	imageFileDir.width  != range_width *(grid_size-1) + 1	||
		imageFileDir.height != range_height*(grid_size-1) + 1	)
	{
		ErrorFilenamef(file_color->nameptr, "ERROR - Corrupted Terrain Tiff File: \nInvalid image size for color data.\nExpected: width=%d, height=%d", range_width *(grid_size-1) + 1 , range_height*(grid_size-1) + 1);
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

    color_buffers = calloc(1, sizeof(void*)*(range_width+1));

	strip = calloc(imageFileDir.width * imageFileDir.rows_per_strip,  sizeof(U8)*3);
    i = 0;
    remaining_lines = 0;
    for (y = 0; y < imageFileDir.height; y++)
    {
        if (remaining_lines == 0)
        {
            remaining_lines = imageFileDir.height-y;
            if (remaining_lines > imageFileDir.rows_per_strip)
                remaining_lines = imageFileDir.rows_per_strip;

            if (i >= (int)imageFileDir.strip_count ||
                !tiffReadStrip(file_color, &imageFileDir, strip, remaining_lines, i))
            {
				free(color_buffers);
                free(strip);
                free(imageFileDir.strip_offsets);
                free(imageFileDir.strip_byte_counts);
                return false;
            }
            strip_ptr = strip;
            i++;
        }

        if ((y % (grid_size-1)) == 0)
        {
            // Last line copied into old tiles
            importTiffColorLoadLine(color_buffers, strip_ptr, imageFileDir.width, 0, lod, true);
            for (map_x = 0; map_x < range_width; map_x++)
            {
                bool found = false;
                IVec2 pos = { map_x+min_pos[0], (range_height-1) - map_y + min_pos[1] };
                color_buffers[map_x] = NULL;
                for (l = 0; l < eaSize(&info->blocks); l++)
                    if (pos[0] >= info->blocks[l]->min[0] &&
                        pos[1] >= info->blocks[l]->min[1] &&
                        pos[0] <= info->blocks[l]->max[0] &&
                        pos[1] <= info->blocks[l]->max[1])
                    {
                        found = true;
                        break;
                    }
                if (found)
                    if ((color_buffers[map_x] = importTiffColorLoadGetBuffer(heightmaps, pos[0], pos[1], lod)) != NULL)
                    {		
                        assert(color_buffers[map_x]->size == grid_size);
                        assert(color_buffers[map_x]->size == grid_size);
                    }
            }
            importTiffColorLoadLine(color_buffers, strip_ptr, imageFileDir.width, (grid_size-1), lod, false);
            map_y++;
        }
        else
        {
            importTiffColorLoadLine(color_buffers, strip_ptr, imageFileDir.width, (grid_size-1)-(y%(grid_size-1)), lod, true);
        }
        
        remaining_lines--;
        strip_ptr += imageFileDir.width*3;
    }
    free(color_buffers);
	free(strip);
	free(imageFileDir.strip_offsets);
	free(imageFileDir.strip_byte_counts);
	return true;
}

TerrainBuffer *importTiffHeightLoadGetBuffer(StashTable heightmaps, U32 x, U32 y, int lod)
{
	IVec2 local_grid_pos;
	HeightMap *height_map=NULL;
    local_grid_pos[0] = x;
    local_grid_pos[1] = y;
    if(!stashIntFindPointer(heightmaps, getTerrainGridPosKey(local_grid_pos), &height_map))
        return NULL;
    return heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, lod);
}

void importTiffHeightLoadLine(TerrainBuffer **height_buffers, F32 min, F32 max, U8 *strip, int strip_width, int y, int lod, bool swizzle)
{
    F32 scale = (max-min)*(1.f/255.f);
    int x, idx = 0;
    int grid_size = GRID_LOD(lod);
    for(x=0; x < strip_width; x++)
    {
        U32 buff_y = y;
        U32 buff_x = x % (grid_size-1);
        assert(	buff_x < (U32)grid_size &&
                buff_y < (U32)grid_size );
        if(swizzle && x != 0)
        {
            strip[x*3 + 0] += strip[(x-1)*3 + 0];
        }
        if (buff_x == 0 && x > 0)
        {
            if (height_buffers[idx])
            {
                height_buffers[idx]->data_f32[grid_size-1 + buff_y*grid_size] = strip[x*3 + 0]*scale + min;
            }
            idx++;
        }
        if (height_buffers[idx])
        {
            height_buffers[idx]->data_f32[buff_x + buff_y*grid_size] = strip[x*3 + 0]*scale + min;
        }
    }
   
}

bool terrainImportHeightTiff(StashTable heightmaps, TerrainExportInfo *info, FILE *file_height, int lod)
{
	int i, l;
    U32 map_x, y;
	U8 *strip, *strip_ptr;
	U32 idf_offset=0;
	U32 grid_size = GRID_LOD(lod);
	TiffIFD imageFileDir = {0};
	U32 remaining_lines;
	U32 map_y=0;
	U32 range_width;
	U32 range_height;
    IVec2 min_pos, max_pos;
	TerrainBuffer **height_buffers;

    if (eaSize(&info->blocks) < 1)
        return false;
    
	if(!tiffReadHeader(file_height, &idf_offset))
		return false;

    copyVec2(info->blocks[0]->min, min_pos);
    copyVec2(info->blocks[0]->max, max_pos);
    for (i = 1; i < eaSize(&info->blocks); i++)
    {
        if (info->blocks[i]->min[0] < min_pos[0])
            min_pos[0] = info->blocks[i]->min[0];
        if (info->blocks[i]->min[1] < min_pos[1])
            min_pos[1] = info->blocks[i]->min[1];
        if (info->blocks[i]->max[0] > max_pos[0])
            max_pos[0] = info->blocks[i]->max[0];
        if (info->blocks[i]->max[1] > max_pos[1])
            max_pos[1] = info->blocks[i]->max[1];
    }
    range_width = max_pos[0]+1-min_pos[0];
    range_height = max_pos[1]+1-min_pos[1];

	if(!tiffReadIFD(file_height, &imageFileDir, idf_offset))
	{
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(imageFileDir.bytesPerChannel != 1)
	{
		ErrorFilenamef(file_height->nameptr, "ERROR - Bad Tiff Format: Invalid Bits Per Sample");
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(imageFileDir.horizontalDifferencingType != TIFF_DIFF_HORIZONTAL)
	{
		ErrorFilenamef(file_height->nameptr, "ERROR - Bad Tiff Format: Not using horizontal differencing");
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(!imageFileDir.lzwCompressed)
	{
		ErrorFilenamef(file_height->nameptr, "ERROR - Bad Tiff Format: Not using LZW compression");
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

	if(	imageFileDir.width  != range_width *(grid_size-1) + 1	||
		imageFileDir.height != range_height*(grid_size-1) + 1	)
	{
		ErrorFilenamef(file_height->nameptr, "ERROR - Corrupted Terrain Tiff File: \nInvalid image size for height data.\nExpected: width=%d, height=%d", range_width *(grid_size-1) + 1 , range_height*(grid_size-1) + 1);
		free(imageFileDir.strip_offsets);
		free(imageFileDir.strip_byte_counts);
		return false;
	}

    height_buffers = calloc(1, sizeof(void*)*(range_width+1));

	strip = calloc(imageFileDir.width * imageFileDir.rows_per_strip,  sizeof(U8)*3);
    i = 0;
    remaining_lines = 0;
    for (y = 0; y < imageFileDir.height; y++)
    {
        if (remaining_lines == 0)
        {
            remaining_lines = imageFileDir.height-y;
            if (remaining_lines > imageFileDir.rows_per_strip)
                remaining_lines = imageFileDir.rows_per_strip;

            if (i >= (int)imageFileDir.strip_count ||
                !tiffReadStrip(file_height, &imageFileDir, strip, remaining_lines, i))
            {
				free(height_buffers);
                free(strip);
                free(imageFileDir.strip_offsets);
                free(imageFileDir.strip_byte_counts);
                return false;
            }
            strip_ptr = strip;
            i++;
        }

        if ((y % (grid_size-1)) == 0)
        {
            // Last line copied into old tiles
            importTiffHeightLoadLine(height_buffers, info->height_min, info->height_max, strip_ptr, imageFileDir.width, 0, lod, true);
            for (map_x = 0; map_x < range_width; map_x++)
            {
                bool found = false;
                IVec2 pos = { map_x+min_pos[0], (range_height-1) - map_y + min_pos[1] };
                height_buffers[map_x] = NULL;
                for (l = 0; l < eaSize(&info->blocks); l++)
                    if (pos[0] >= info->blocks[l]->min[0] &&
                        pos[1] >= info->blocks[l]->min[1] &&
                        pos[0] <= info->blocks[l]->max[0] &&
                        pos[1] <= info->blocks[l]->max[1])
                    {
                        found = true;
                        break;
                    }
                if (found)
                    if ((height_buffers[map_x] = importTiffHeightLoadGetBuffer(heightmaps, pos[0], pos[1], lod)) != NULL)
                    {		
                        assert(height_buffers[map_x]->size == grid_size);
                        assert(height_buffers[map_x]->size == grid_size);
                    }
            }
            importTiffHeightLoadLine(height_buffers, info->height_min, info->height_max, strip_ptr, imageFileDir.width, (grid_size-1), lod, false);
            map_y++;
        }
        else
        {
            importTiffHeightLoadLine(height_buffers, info->height_min, info->height_max, strip_ptr, imageFileDir.width, (grid_size-1)-(y%(grid_size-1)), lod, true);
        }
        
        remaining_lines--;
        strip_ptr += imageFileDir.width*3;
    }
    free(height_buffers);
	free(strip);
	free(imageFileDir.strip_offsets);
	free(imageFileDir.strip_byte_counts);
	return true;
}

static bool terrainBlockDoLoadSource(ZoneMapLayer *layer, TerrainBlockRange *range, IVec3 min, IVec3 max,
									 HeightMap ***cache, bool is_neighbor, bool validate, LibFileLoad *layer_load)
{
	size_t count;
	U32 l, i, x, y, grid_size;
    U32 color_grid_size;
	int heightmap_lod = -1;
	int best_normal_lod = -1;
    int compressed = 0;
    bool failure[TERRAIN_BUFFER_NUM_TYPES];
    bool missing[TERRAIN_BUFFER_NUM_TYPES];
	TerrainFileList *list = &range->source_files;
    int lod = -1;

    for (i = 0; i < TERRAIN_BUFFER_NUM_TYPES; i++)
        failure[i] = missing[i] = false;

	if (range->range.max_block[0] < range->range.min_block[0] || 
		range->range.max_block[2] < range->range.min_block[2])
		return false;

	// Single-file heightmap, new format
	if (openTerrainFileReadSource(&list->heightmap, validate))
	{
        TerrainBuffer *occlusion_buffer;
        HeightMap **maps;
		U32 size[2];
		count = fread(&size[0], sizeof(U32), 1, list->heightmap.file);
		if (count != 1)
            failure[TERRAIN_BUFFER_HEIGHT] = true;
		count = fread(&size[1], sizeof(U32), 1, list->heightmap.file);
		if (count != 1)
            failure[TERRAIN_BUFFER_HEIGHT] = true;
		count = fread(&l, sizeof(U32), 1, list->heightmap.file);
		if (count != 1)
            failure[TERRAIN_BUFFER_HEIGHT] = true;
		if ((l & 0xff) > HEIGHTMAP_MAX_LOD)
			failure[TERRAIN_BUFFER_HEIGHT] = true;
        if (!failure[TERRAIN_BUFFER_HEIGHT])
        {
            lod = l & 0xff;
            compressed = (l >> 8);
            l = 0;
            verbose_printf("Loaded source for block %d, LOD %d\n", range->block_idx, lod);
            if (size[0] < (U32)(range->range.max_block[0]+1-range->range.min_block[0]) ||
                size[1] < (U32)(range->range.max_block[2]+1-range->range.min_block[2]))
            {
                for (y = 0; y <= (U32)(range->range.max_block[2]-range->range.min_block[2]); y++)
                    for (x = size[0]; x <= (U32)(range->range.max_block[0]-range->range.min_block[0]); x++)
                        if (((S32)x + range->range.min_block[0]) >= min[0] && ((S32)x + range->range.min_block[0]) <= max[0] &&
                            ((S32)y + range->range.min_block[2]) >= min[2] && ((S32)y + range->range.min_block[2]) <= max[2])
                        {
                            HeightMap *height_map = heightMapCreateDefault(layer, lod, x + range->range.min_block[0], y + range->range.min_block[2]);
                            eaPush(cache, height_map);
                        }
                for (y = size[1]; y <= (U32)(range->range.max_block[2]-range->range.min_block[2]); y++)
                    for (x = 0; x < size[0]; x++)
                        if (((S32)x + range->range.min_block[0]) >= min[0] && ((S32)x + range->range.min_block[0]) <= max[0] &&
                            ((S32)y + range->range.min_block[2]) >= min[2] && ((S32)y + range->range.min_block[2]) <= max[2])
                        {
                            HeightMap *height_map = heightMapCreateDefault(layer, lod,  x + range->range.min_block[0], y + range->range.min_block[2]);
                            eaPush(cache, height_map);
                        }
                verbose_printf("Resized block from %dx%d to %dx%d\n", size[0], size[1], (range->range.max_block[0]+1-range->range.min_block[0]), (range->range.max_block[2]+1-range->range.min_block[2]));
            }
            maps = calloc(1, size[0]*size[1]*sizeof(void*));
            grid_size = GRID_LOD(lod);
            for (y = 0; y < size[1] && !failure[TERRAIN_BUFFER_HEIGHT]; y++)
                for (x = 0; x < size[0] && !failure[TERRAIN_BUFFER_HEIGHT]; x++)
                {
                    U32 compressed_size = 0;
                    TerrainBuffer *height_buffer = NULL;
                    HeightMap *height_map = NULL;
                    if (((S32)x + range->range.min_block[0]) >= min[0] && ((S32)x + range->range.min_block[0]) <= max[0] &&
                        ((S32)y + range->range.min_block[2]) >= min[2] && ((S32)y + range->range.min_block[2]) <= max[2])
                    {
                        height_map = heightMapCreate(layer, x + range->range.min_block[0], y + range->range.min_block[2], NULL);
                        height_map->size = grid_size;
                        height_map->loaded_level_of_detail = lod;
                        height_map->level_of_detail = lod;
                        height_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_HEIGHT, lod);

                        eaPush(cache, height_map);
	                    heightMapCreateBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, lod);
                    	maps[x+y*size[0]] = height_map;
                    }
                    if (compressed > 0)
                    {
                        count = fread(&compressed_size, sizeof(U32), 1, list->heightmap.file);
                        if (count != 1)
                            failure[TERRAIN_BUFFER_HEIGHT] = true;
                    }
                    if (compressed_size > 0)
                    {
                        U32 out_size;
                        U8 *data = terrainAlloc(1, compressed_size);
						if (!data)
							return false;
                        count = fread(data, 1, compressed_size, list->heightmap.file);
                        if (count != compressed_size)
                            failure[TERRAIN_BUFFER_HEIGHT] = true;
                        if (height_buffer)
                        {
                            out_size = sizeof(F32)*grid_size*grid_size;
                            unzipData(height_buffer->data_byte, &out_size,
                                      data, compressed_size);
                        }
                        SAFE_FREE(data);
                        if (height_buffer)
	                        for (i = 1; i < grid_size*grid_size; i++)
    	                        height_buffer->data_f32[i] += height_buffer->data_f32[i-1];
                    }
                    else
                    {
                        if (height_buffer)
                        {
                        	count = fread(height_buffer->data_f32, sizeof(F32), grid_size*grid_size, list->heightmap.file);
	                        if (count != grid_size*grid_size)
    	                        failure[TERRAIN_BUFFER_HEIGHT] = true;
                        }
                        else
                            fseek(list->heightmap.file, grid_size*grid_size*sizeof(F32), SEEK_CUR);
                    }
                }
            for (y = 0; y < size[1]; y++)
                for (x = 0; x < size[0]; x++)
                {
                    if (maps[x+y*size[0]])
                    {
                    	occlusion_buffer = heightMapCreateBuffer(maps[x+y*size[0]], TERRAIN_BUFFER_OCCLUSION, 5);
	                    fread(occlusion_buffer->data_byte, 1, 16, list->heightmap.file);
                    }
                    else
                        fseek(list->heightmap.file, 16, SEEK_CUR);
                }
            SAFE_FREE(maps);
        }
        closeTerrainFile(&list->heightmap);
	}
    else
        missing[TERRAIN_BUFFER_HEIGHT] = true;
    
	if (failure[TERRAIN_BUFFER_HEIGHT] || missing[TERRAIN_BUFFER_HEIGHT])
	{
        eaDestroyEx(cache, heightMapDestroy);
		lod = HEIGHTMAP_DEFAULT_LOD;
		grid_size = GRID_LOD(lod);
		for (y = 0; y <= (U32)(range->range.max_block[2]-range->range.min_block[2]); y++)
			for (x = 0; x <= (U32)(range->range.max_block[0]-range->range.min_block[0]); x++)
			{
				HeightMap *height_map = heightMapCreateDefault(layer, lod, x + range->range.min_block[0], y + range->range.min_block[2]);
				eaPush(cache, height_map);
			}
		if (!layerIsGenesis(layer))
			ErrorFilenamef(list->heightmap.file_base.fullname, "Missing source data for terrain block!");
	}

	// Color map
	for (i = 0; i < eaUSize(cache); i++)
	{
		for(x = GET_COLOR_LOD(lod); x < GET_COLOR_LOD(MAX_TERRAIN_LODS); x++)
		{
			TerrainBuffer *color_buffer;
			color_grid_size = GRID_LOD(x);
			color_buffer = heightMapGetBuffer((*cache)[i], TERRAIN_BUFFER_COLOR, x);
			if(color_buffer == NULL)
			{
				color_buffer = heightMapCreateBuffer((*cache)[i], TERRAIN_BUFFER_COLOR, x);
				for (y = 0; y < color_grid_size*color_grid_size; y++)
				{
					setVec3(color_buffer->data_u8vec3[y], ((y%2) ? 255 : 0), ((y%2) ? 0 : 255),  ((y%2) ? 255 : 0));
				}
			}
		}
	}
	if(openTerrainFileReadSource(&list->tiffcolormap, validate))
	{
		if (!heightMapTiffColorLoad(layer, &list->tiffcolormap, range, GET_COLOR_LOD(lod), cache))
			failure[TERRAIN_BUFFER_COLOR] = true;
		closeTerrainFile(&list->tiffcolormap);	
	}

    if (is_neighbor)
        return true;

	// Alpha map
	if (openTerrainFileReadSource(&list->holemap, validate))
	{
		U32 size[2];
		count = fread(&size[0], sizeof(U32), 1, list->holemap.file);
		if (count != 1)
            failure[TERRAIN_BUFFER_ALPHA] = true;
		count = fread(&size[1], sizeof(U32), 1, list->holemap.file);
		if (count != 1)
            failure[TERRAIN_BUFFER_ALPHA] = true;
		count = fread(&l, sizeof(U32), 1, list->holemap.file);
		if (count != 1)
            failure[TERRAIN_BUFFER_ALPHA] = true;
        if (!failure[TERRAIN_BUFFER_ALPHA] && (l & 0xff) == lod)
        {
            compressed = (l >> 8);
            l = 0;

            for (y = 0; y < size[1]; y++)
                for (x = 0; x < size[0]; x++)
                {
                    U32 compressed_size = 0;
                    HeightMap *height_map = NULL;
                    TerrainBuffer *alpha_buffer;

                    if (compressed > 0)
                    {
                        count = fread(&compressed_size, sizeof(U32), 1, list->holemap.file);
                        if (count != 1)
                            failure[TERRAIN_BUFFER_ALPHA] = true;
                    }
                    
                    for (i = 0; i < eaUSize(cache); i++)
                        if ((*cache)[i]->map_local_pos[0] == x+range->range.min_block[0] &&
                            (*cache)[i]->map_local_pos[1] == y+range->range.min_block[2])
                        {
                            height_map = (*cache)[i];
                            break;
                        }
                    if (!height_map)
                    {
                        if (compressed_size > 0)
                            fseek(list->holemap.file, compressed_size, SEEK_CUR);
                        else
                            fseek(list->holemap.file, grid_size*grid_size, SEEK_CUR);
                        continue;
                    }

                    alpha_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_ALPHA, lod);

                    if (compressed_size > 0)
                    {
                        U32 out_size;
						bool found_hole = false;
                        U8 *data = terrainAlloc(1, compressed_size);
						if (!data)
							return false;
                        count = fread(data, 1, compressed_size, list->holemap.file);
                        if (count != compressed_size)
                            failure[TERRAIN_BUFFER_ALPHA] = true;
                        out_size = grid_size*grid_size;
                        unzipData(alpha_buffer->data_byte, &out_size,
                                  data, compressed_size);
                        SAFE_FREE(data);

						for (i = 0; i < grid_size*grid_size; i++)
							if (alpha_buffer->data_byte[i] != 255)
							{
								found_hole = true;
								break;
							}
						if (!found_hole)
							heightMapDeleteBuffer(height_map, TERRAIN_BUFFER_ALPHA, lod);
                    }
                    else
                    {
                        count = fread(alpha_buffer->data_byte, 1, grid_size*grid_size, list->holemap.file);
                        if (count != grid_size*grid_size)
                            failure[TERRAIN_BUFFER_ALPHA] = true;
                    }
                }
        }
		closeTerrainFile(&list->holemap);
	}
    else
        missing[TERRAIN_BUFFER_ALPHA] = true;

	// Material map
	if (openTerrainFileReadSource(&list->materialmap, validate))
	{
		U32 size[2];
		count = fread(&size[0], sizeof(U32), 1, list->materialmap.file);
		if (count != 1)
            failure[TERRAIN_BUFFER_MATERIAL] = true;
		count = fread(&size[1], sizeof(U32), 1, list->materialmap.file);
		if (count != 1)
            failure[TERRAIN_BUFFER_MATERIAL] = true;
		count = fread(&l, sizeof(U32), 1, list->materialmap.file);
		if (count != 1)
            failure[TERRAIN_BUFFER_MATERIAL] = true;
        if (!failure[TERRAIN_BUFFER_MATERIAL] && (l & 0xff) == lod)
        {
            compressed = (l >> 8);
            l = 0;
            for (y = 0; y < size[1]; y++)
                for (x = 0; x < size[0]; x++)
                {
                    U32 compressed_size = 0;
                    HeightMap *height_map = NULL;
                    TerrainBuffer *material_buffer;
                    U8 material_count;
                    U8 material_ids[NUM_MATERIALS];

                    for (i = 0; i < eaUSize(cache); i++)
                        if ((*cache)[i]->map_local_pos[0] == x+range->range.min_block[0] &&
                            (*cache)[i]->map_local_pos[1] == y+range->range.min_block[2])
                        {
                            height_map = (*cache)[i];
                            break;
                        }

                    fread(&material_count, sizeof(U8), 1, list->materialmap.file);
                    fread(material_ids, sizeof(U8), NUM_MATERIALS, list->materialmap.file);

                    if (compressed > 0)
                    {
                        count = fread(&compressed_size, sizeof(U32), 1, list->materialmap.file);
                        if (count != 1)
                            failure[TERRAIN_BUFFER_MATERIAL] = true;
                    }

                    if (!height_map)
                    {
                        if (compressed_size == 0)
                            fseek(list->materialmap.file, grid_size*grid_size*SRC_MATERIAL_SIZE, SEEK_CUR);
                        else
                            fseek(list->materialmap.file, compressed_size, SEEK_CUR);
                        continue;
                    }                    

                    height_map->material_count = material_count;
                    memcpy(height_map->material_ids, material_ids, NUM_MATERIALS);
                    
                    material_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_MATERIAL, lod);

                    if (compressed_size > 0)
                    {
                        U32 out_size;
                        U8 *data = terrainAlloc(1, compressed_size);
						if (!data)
							return false;
                        count = fread(data, 1, compressed_size, list->materialmap.file);
                        if (count != compressed_size)
                            failure[TERRAIN_BUFFER_MATERIAL] = true;
                        out_size = NUM_MATERIALS*grid_size*grid_size;
                        unzipData(material_buffer->data_byte, &out_size,
                                  data, compressed_size);
                        SAFE_FREE(data);
                    }
                    else
                    {
                        count = fread(material_buffer->data_byte, NUM_MATERIALS, grid_size*grid_size, list->materialmap.file);
                        if (count != grid_size*grid_size)
                            failure[TERRAIN_BUFFER_MATERIAL] = true;
                    }
                    // Fun with unsigned ints!
                    for (i = grid_size*grid_size*NUM_MATERIALS; i > 0; ) 
                    {
                        --i;
                        material_buffer->data_f32[i] = (F32)material_buffer->data_byte[i];
                    }
                    if (!heightMapValidateMaterials(height_map, NULL, layer_load ? layer_load->material_table : layer->terrain.material_table))
                    {
                        ErrorFilenamef(list->materialmap.file_base.fullname, "Corrupted terrain data: Materials may be incorrect.");
						failure[TERRAIN_BUFFER_MATERIAL] = true;
						if (wlIsServer())
						{
							// Fix up so binning doesn't crash
							height_map->material_count = 1;
							memset(material_buffer->data_f32, 0, grid_size*grid_size*NUM_MATERIALS*sizeof(F32));
						}
                    }
                }
        }
        closeTerrainFile(&list->materialmap);
	}
    else
        missing[TERRAIN_BUFFER_MATERIAL] = true;

    if (!wlIsServer())
    {
        // Soil Depth map
        if (openTerrainFileReadSource(&list->soildepthmap, validate))
        {
            U32 size[2];
            count = fread(&size[0], sizeof(U32), 1, list->soildepthmap.file);
            if (count != 1)
                failure[TERRAIN_BUFFER_SOIL_DEPTH] = true;
            count = fread(&size[1], sizeof(U32), 1, list->soildepthmap.file);
            if (count != 1)
                failure[TERRAIN_BUFFER_SOIL_DEPTH] = true;
            count = fread(&l, sizeof(U32), 1, list->soildepthmap.file);
            if (count != 1)
                failure[TERRAIN_BUFFER_SOIL_DEPTH] = true;
            if (!failure[TERRAIN_BUFFER_SOIL_DEPTH] && (l & 0xff) == lod)
            {
                compressed = (l >> 8);
                l = 0;
                for (y = 0; y < size[1]; y++)
                    for (x = 0; x < size[0]; x++)
                    {
                        U32 compressed_size = 0;
                        HeightMap *height_map = NULL;
                        TerrainBuffer *soil_depth_buffer;

                        if (compressed > 0)
                        {
                            count = fread(&compressed_size, sizeof(U32), 1, list->soildepthmap.file);
                            if (count != 1)
                                failure[TERRAIN_BUFFER_SOIL_DEPTH] = true;
                        }

                        for (i = 0; i < eaUSize(cache); i++)
                            if ((*cache)[i]->map_local_pos[0] == x+range->range.min_block[0] &&
                                (*cache)[i]->map_local_pos[1] == y+range->range.min_block[2])
                            {
                                height_map = (*cache)[i];
                                break;
                            }
                    
                        if (!height_map)
                        {
                            if (compressed_size == 0)
                                fseek(list->soildepthmap.file, grid_size*grid_size*SRC_SOIL_DEPTH_SIZE, SEEK_CUR);
                            else
                                fseek(list->soildepthmap.file, compressed_size, SEEK_CUR);
                            continue;
                        }
				
                        soil_depth_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, lod);
                        if(!soil_depth_buffer)
                            soil_depth_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, lod);

                        if (compressed_size > 0)
                        {
                            U32 out_size;
                            U8 *data = terrainAlloc(1, compressed_size);
							if (!data)
								return false;
                            count = fread(data, 1, compressed_size, list->soildepthmap.file);
                            if (count != compressed_size)
                                failure[TERRAIN_BUFFER_SOIL_DEPTH] = true;
                            out_size = sizeof(F32)*grid_size*grid_size;
                            unzipData(soil_depth_buffer->data_byte, &out_size,
                                      data, compressed_size);
                            SAFE_FREE(data);
                        }
                        else
                        {
                            count = fread(soil_depth_buffer->data_f32, sizeof(F32), grid_size*grid_size, list->soildepthmap.file);
                            if (count != grid_size*grid_size)
                                failure[TERRAIN_BUFFER_SOIL_DEPTH] = true;
                        }
                    }
            }
            closeTerrainFile(&list->soildepthmap);	
        }
        else
            missing[TERRAIN_BUFFER_SOIL_DEPTH] = true;
    }

	// Terrain Object map
	if (openTerrainFileReadSource(&list->objectmap, validate))
	{
		U32 num_maps;
		count = fread(&num_maps, sizeof(U32), 1, list->objectmap.file);
        if (count != 1)
            failure[TERRAIN_BUFFER_OBJECTS] = true;
		count = fread(&l, sizeof(U32), 1, list->objectmap.file);
        if (count != 1)
            failure[TERRAIN_BUFFER_OBJECTS] = true;
        if (!failure[TERRAIN_BUFFER_OBJECTS] && (l & 0xff) == lod)
        {
            compressed = (l >> 8);
            l = 0;
		
            for(x=0; x < num_maps; x++)
            {
                IVec2 map_pos;
                U32 compressed_size = 0;
                U8 *dest_buf, *dest_buf_ptr;
                U32 dest_buf_size;
                U8 num_sub_buffers;
                HeightMap *height_map = NULL;
                TerrainBuffer *object_buffer;

                fread(&map_pos, sizeof(IVec2), 1, list->objectmap.file);
                fread(&num_sub_buffers, sizeof(U8), 1, list->objectmap.file);

                if (num_sub_buffers == 0)
                    continue;
                
                dest_buf_size = num_sub_buffers*(1+(grid_size*grid_size*sizeof(TerrainObjectDensity)));
                if (compressed > 0)
                {
                    count = fread(&compressed_size, sizeof(U32), 1, list->objectmap.file);
                    if (count != 1)
                        failure[TERRAIN_BUFFER_OBJECTS] = true;
                }

                for (i = 0; i < eaUSize(cache); i++)
                    if ((*cache)[i]->map_local_pos[0] == map_pos[0]+range->range.min_block[0] &&
                        (*cache)[i]->map_local_pos[1] == map_pos[1]+range->range.min_block[2])
                    {
                        height_map = (*cache)[i];
                        break;
                    }
                if (!height_map)
                {
                    if (compressed_size == 0)
                        fseek(list->objectmap.file, dest_buf_size, SEEK_CUR);
                    else
                        fseek(list->objectmap.file, compressed_size, SEEK_CUR);
                    continue;
                }

                object_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_OBJECTS, lod);

                dest_buf = terrainAlloc(1, dest_buf_size);
				if (!dest_buf)
					return false;
                if (compressed_size > 0)
                {
                    U32 out_size;
                    U8 *data = terrainAlloc(1, compressed_size);
					if (!data)
						return false;
                    count = fread(data, 1, compressed_size, list->objectmap.file);
                    if (count != compressed_size)
                        failure[TERRAIN_BUFFER_OBJECTS] = true;
                    out_size = dest_buf_size;
                    unzipData(dest_buf, &out_size,
                              data, compressed_size);
                    SAFE_FREE(data);
                }
                else
                {
                    count = fread(dest_buf, 1, dest_buf_size, list->objectmap.file);
                    if (count != dest_buf_size)
                        failure[TERRAIN_BUFFER_OBJECTS] = true;
                }

                dest_buf_ptr = dest_buf;

                for(i=0; i < num_sub_buffers; i++)
                {
                    U8 type = *(dest_buf_ptr++);
					int layer_table_size = layer_load ? eaSize(&layer_load->object_table) : eaSize(&layer->terrain.object_table);
                    if (type < layer_table_size)
                    {
	                    TerrainObjectBuffer *new_sub_buffer = terrainAlloc(sizeof(TerrainObjectBuffer),1);
						if (!new_sub_buffer)
							return false;

						new_sub_buffer->density = terrainAlloc(grid_size*grid_size, sizeof(TerrainObjectDensity));
						if (!new_sub_buffer->density)
							return false;

    					new_sub_buffer->object_type = type;
        				memcpy(new_sub_buffer->density, dest_buf_ptr, grid_size*grid_size*sizeof(TerrainObjectDensity));
				
                		eaPush(&object_buffer->data_objects, new_sub_buffer);
                    }
                    else
                    {
                        ErrorFilenamef(list->objectmap.file_base.fullname, "Corrupted terrain data: Missing object table entry");
                    }
                    dest_buf_ptr += grid_size*grid_size*sizeof(TerrainObjectDensity);
                }
                SAFE_FREE(dest_buf);
            }
        }
        
		closeTerrainFile(&list->objectmap);
	}
    else
        missing[TERRAIN_BUFFER_OBJECTS] = true;
    
    if (failure[TERRAIN_BUFFER_HEIGHT] || failure[TERRAIN_BUFFER_ALPHA] ||
        failure[TERRAIN_BUFFER_MATERIAL] || failure[TERRAIN_BUFFER_SOIL_DEPTH] ||
        failure[TERRAIN_BUFFER_OBJECTS])
    {
        if (failure[TERRAIN_BUFFER_HEIGHT])
			ErrorFilenamef(list->heightmap.file_base.fullname, "Corrupted terrain source data: height!");
        else if (failure[TERRAIN_BUFFER_ALPHA])
        	ErrorFilenamef(list->holemap.file_base.fullname, "Corrupted terrain source data: alpha!");
        else if (failure[TERRAIN_BUFFER_MATERIAL])
        	ErrorFilenamef(list->materialmap.file_base.fullname, "Corrupted terrain source data: material!");
        else if (failure[TERRAIN_BUFFER_SOIL_DEPTH])
        	ErrorFilenamef(list->soildepthmap.file_base.fullname, "Corrupted terrain source data: soil!");
        else if (failure[TERRAIN_BUFFER_OBJECTS])
        	ErrorFilenamef(list->objectmap.file_base.fullname, "Corrupted terrain source data: objects!");
		return false;
    }    
	return true;
}

void terrainBlockLoadSource(ZoneMapLayer *layer, TerrainBlockRange *range, bool neighbors)
{
	U32 i;
    if (neighbors)
    {
        IVec3 min, max;
        min[0] = range->range.min_block[0]-1;
        min[2] = range->range.min_block[2]-1;
        max[0] = range->range.max_block[0]+1;
        max[2] = range->range.max_block[2]+1;
        for (i = 0; i < eaUSize(&layer->terrain.blocks); i++) 
        	terrainBlockDoLoadSource(layer, layer->terrain.blocks[i], min, max,
                                     &range->map_cache, (layer->terrain.blocks[i] != range), false, NULL);
    }
    else
    {
    	terrainBlockDoLoadSource(layer, range, range->range.min_block, range->range.max_block, &range->map_cache, false, false, NULL);
    }
	for (i = 0; i < eaUSize(&range->map_cache); i++)
	{
		HeightMap *height_map = range->map_cache[i];
        int lod = height_map->loaded_level_of_detail;
	    int grid_size = GRID_LOD(lod);
		if (!heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, lod))
			heightMapCreateBuffer(height_map, TERRAIN_BUFFER_MATERIAL, lod);
		if (!heightMapGetBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, lod))
			heightMapCreateBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, lod);
		if (!heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, lod))
			heightMapCreateBuffer(height_map, TERRAIN_BUFFER_OBJECTS, lod);
		if (!heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod))
			heightMapCreateBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
	}
}

static void heightMapNormalCreate(int mip, HeightMap *height_map)
{
	U32 i, size;
	TerrainBuffer *normal_buffer, *color_buffer;
	size = GRID_LOD(mip);

	normal_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_NORMAL, mip);
	if (!normal_buffer)
		normal_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_NORMAL, mip);

	color_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, mip);
	if (!color_buffer)
		color_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_COLOR, mip);

	for (i = 0; i < size * size; ++i)
	{
		setVec3(normal_buffer->data_normal[i], 128, 255, 128);
		setVec3(color_buffer->data_u8vec3[i], 128, 128, 128);
	}
}

void terrainExportTiff(TerrainBlockRange **blocks, StashTable heightmaps, int lod, FILE *file_height, FILE *file_color, F32 *min, F32 *max)
{
	int i;
	U32 j, x, y;
	TiffIFD imageFileDirHeight = {0}, imageFileDirColor = {0};
	U32 size = GRID_LOD(lod);
    U32 size_color = GRID_LOD(GET_COLOR_LOD(lod));
	U32 IDF_location;
	U8 *strip_height, *strip_color;
	IVec2 min_pos, max_pos;
	int block_width, block_height;
	F32 min_height, max_height, height_scale;

	if (eaSize(&blocks) < 1)
		return;

    setVec2(min_pos, blocks[0]->range.min_block[0], blocks[0]->range.min_block[2]);
    setVec2(max_pos, blocks[0]->range.max_block[0], blocks[0]->range.max_block[2]);

	// Compute min and max
    for (i = 1; i < eaSize(&blocks); i++)
    {
        min_pos[0] = MIN(blocks[i]->range.min_block[0], min_pos[0]);
        min_pos[1] = MIN(blocks[i]->range.min_block[2], min_pos[1]);
        max_pos[0] = MAX(blocks[i]->range.max_block[0], max_pos[0]);
        max_pos[1] = MAX(blocks[i]->range.max_block[2], max_pos[1]);
    }

	block_width = (max_pos[0]+1-min_pos[0]);
	block_height = (max_pos[1]+1-min_pos[1]);
	
	imageFileDirHeight.width = ((size-1) * block_width) + 1;
	imageFileDirHeight.height = ((size-1) * block_height) + 1;
	imageFileDirHeight.bytesPerChannel = 1;
	imageFileDirHeight.numChannels = 3;
	imageFileDirHeight.lzwCompressed = true;
	imageFileDirHeight.horizontalDifferencingType = TIFF_DIFF_HORIZONTAL;
	imageFileDirHeight.rows_per_strip = (size-1);
	imageFileDirHeight.strip_count = block_height + 1;
	
	imageFileDirHeight.strip_offsets = calloc(imageFileDirHeight.strip_count, sizeof(U32));
	imageFileDirHeight.strip_byte_counts = calloc(imageFileDirHeight.strip_count, sizeof(U32));
	
	imageFileDirColor.width = ((size_color-1) * block_width) + 1;
	imageFileDirColor.height = ((size_color-1) * block_height) + 1;
	imageFileDirColor.bytesPerChannel = 1;
	imageFileDirColor.numChannels = 3;
	imageFileDirColor.lzwCompressed = true;
	imageFileDirColor.horizontalDifferencingType = TIFF_DIFF_HORIZONTAL;
	imageFileDirColor.rows_per_strip = (size_color-1);
	imageFileDirColor.strip_count = block_height + 1;
	
	imageFileDirColor.strip_offsets = calloc(imageFileDirColor.strip_count, sizeof(U32));
	imageFileDirColor.strip_byte_counts = calloc(imageFileDirColor.strip_count, sizeof(U32));

	//Fill with 0 for now, will put correct offset in later
	tiffWriteHeader(file_height, 0, 0);
	tiffWriteHeader(file_color, 0, 0);

	// Calculate min/max values
	min_height = 50000.f;
	max_height = -50000.f;
	for(j = 0; j < imageFileDirHeight.strip_count-1; j++)
    {
        IVec2 local_pos;
        local_pos[1] = min_pos[1] + (imageFileDirHeight.strip_count-2 - j);//Tiff is Y flipped
        
        for (i = block_width-1; i >= 0 ; i--)
        {
            HeightMap *height_map;
            TerrainBuffer *height_buffer;
	
            local_pos[0] = min_pos[0] + i;
            if (stashIntFindPointer(heightmaps, getTerrainGridPosKey(local_pos), &height_map))
            {
                height_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, lod);
			
                for(x = 0; x < size; x++)
                    for(y = 0; y < size; y++)
                    {
                        F32 height = height_buffer->data_f32[x + y*size];
                        if (height < min_height)
                            min_height = height;
                        if (height > max_height)
                            max_height = height;
                    }
            }
        }
    }
	height_scale = 255.f/(max_height-min_height);

    *min = min_height;
    *max=  max_height;

	//For each strip
	strip_height = calloc(imageFileDirHeight.width * (imageFileDirHeight.rows_per_strip+1),  sizeof(U8)*3);
	strip_color = calloc(imageFileDirColor.width * (imageFileDirColor.rows_per_strip+1),  sizeof(U8)*3);
	for(j=0; j < imageFileDirHeight.strip_count-1; j++)
	{
		//Save strip location
		imageFileDirHeight.strip_offsets[j] = ftell(file_height);
		imageFileDirColor.strip_offsets[j] = ftell(file_color);

        memset(strip_height, 0, imageFileDirHeight.width * (imageFileDirHeight.rows_per_strip+1) * sizeof(U8) * 3);
        memset(strip_color, 0, imageFileDirColor.width * (imageFileDirColor.rows_per_strip+1) * sizeof(U8) * 3);

		//Fill strip data
		for(i=0; i < block_width; i++)
        {
            HeightMap *height_map;
            TerrainBuffer *height_buffer, *color_buffer;
            IVec2 local_pos;

            local_pos[0] = min_pos[0] + i;
            local_pos[1] = min_pos[1] + (imageFileDirHeight.strip_count-2 - j);//Tiff is Y flipped
            if (stashIntFindPointer(heightmaps, getTerrainGridPosKey(local_pos), &height_map))
            {
                height_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, lod);
                    
                for(x=0; x < size; x++)
                {
                    int sx = x + i*(size-1);
                    for(y=0; y < size; y++)
                    {
                        int rev_y = size-1 - y;//Tiff is Y flipped
                        U8 value = (U8)CLAMP((height_buffer->data_f32[x + rev_y*size]-min_height)*height_scale, 0, 255);
                        strip_height[(sx + y*imageFileDirHeight.width)*3 + 0] =  value;
                        strip_height[(sx + y*imageFileDirHeight.width)*3 + 1] =  value;
                        strip_height[(sx + y*imageFileDirHeight.width)*3 + 2] =  value;
                    }
                }

                color_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, GET_COLOR_LOD(lod));

                for(x=0; x < size_color; x++)
                {
                    int sx = x + i*(size_color-1);
                    for(y=0; y < size_color; y++)
                    {
                        int rev_y = size_color-1 - y;//Tiff is Y flipped
                        strip_color[(sx + y*imageFileDirColor.width)*3 + 0] = color_buffer->data_u8vec3[x + rev_y*size_color][2];
                        strip_color[(sx + y*imageFileDirColor.width)*3 + 1] = color_buffer->data_u8vec3[x + rev_y*size_color][1];
                        strip_color[(sx + y*imageFileDirColor.width)*3 + 2] = color_buffer->data_u8vec3[x + rev_y*size_color][0];
                    }
                }
            }
        }

        for (y = 0; y < imageFileDirHeight.rows_per_strip+1; y++)
        {
            int offset = y * imageFileDirHeight.width*3;
            for (x = imageFileDirHeight.width-1; x > 0; --x)
            {
                strip_height[x*3 + offset + 0] -= strip_height[x*3 + offset - 3];
                strip_height[x*3 + offset + 1] -= strip_height[x*3 + offset - 2];
                strip_height[x*3 + offset + 2] -= strip_height[x*3 + offset - 1];
            }
        }
        for (y = 0; y < imageFileDirColor.rows_per_strip+1; y++)
        {
            int offset = y * imageFileDirColor.width*3;
            for (x = imageFileDirColor.width-1; x > 0; --x)
            {
                strip_color[x*3 + offset + 0] -= strip_color[x*3 + offset - 3];
                strip_color[x*3 + offset + 1] -= strip_color[x*3 + offset - 2];
                strip_color[x*3 + offset + 2] -= strip_color[x*3 + offset - 1];
            }
        }

		//Compress and Write Data
		imageFileDirHeight.strip_byte_counts[j] = tiffWriteStrip(file_height, &imageFileDirHeight, strip_height, imageFileDirHeight.rows_per_strip);
		imageFileDirColor.strip_byte_counts[j] = tiffWriteStrip(file_color, &imageFileDirColor, strip_color, imageFileDirColor.rows_per_strip);
	}
	//Compress and Write the final strip
	imageFileDirHeight.strip_offsets[j] = ftell(file_height);
	imageFileDirHeight.strip_byte_counts[j] = tiffWriteStrip(file_height, &imageFileDirHeight, strip_height + imageFileDirHeight.rows_per_strip*imageFileDirHeight.width*3*sizeof(U8), 1);
	free(strip_height);
	//Compress and Write the final strip
	imageFileDirColor.strip_offsets[j] = ftell(file_color);
	imageFileDirColor.strip_byte_counts[j] = tiffWriteStrip(file_color, &imageFileDirColor, strip_color + imageFileDirColor.rows_per_strip*imageFileDirColor.width*3*sizeof(U8), 1);
	free(strip_color);

	//Write out the Image File Directory
	IDF_location = ftell(file_height);
	tiffWriteIFD(file_height, &imageFileDirHeight, IDF_location);
	//Write the real header
	tiffWriteHeader(file_height, IDF_location, 0);

	//Write out the Image File Directory
	IDF_location = ftell(file_color);
	tiffWriteIFD(file_color, &imageFileDirColor, IDF_location);
	//Write the real header
	tiffWriteHeader(file_color, IDF_location, 0);
    
	free(imageFileDirHeight.strip_offsets);
	free(imageFileDirHeight.strip_byte_counts);
	free(imageFileDirColor.strip_offsets);
	free(imageFileDirColor.strip_byte_counts);
}

#ifdef TERRAIN_DEBUG_CACHE_HITS
int iDebugCacheQueries = 0;
int iDebugCacheHits = 0;
#endif

HeightMap* terrainFindHeightMap(StashTable heightmaps, IVec2 local_pos, HeightMapCache *cache)
{
    int ix, iy;
    HeightMap *height_map;

    if (cache != NULL)
    {
        ix = local_pos[0] & 1;
        iy = local_pos[1] & 1;
        if (cache->maps[ix][iy] != NULL &&
            sameVec2(cache->maps[ix][iy]->map_local_pos, local_pos))
        {
            assert(cache->maps[ix][iy]->magic == HEIGHTMAP_MAGIC_NUMBER);
#ifdef TERRAIN_DEBUG_CACHE_HITS
            iDebugCacheQueries++;
            iDebugCacheHits++;
            if (iDebugCacheQueries == 10000)
            {
                printf("CACHE HIT RATE %0.02f\n", iDebugCacheHits/100.0f);
                iDebugCacheHits = iDebugCacheQueries = 0;
            }
#endif
            return cache->maps[ix][iy];
        }
    }
    
	if (!stashIntFindPointer(heightmaps, getTerrainGridPosKey(local_pos), &height_map))
	{
		return NULL;
	}
    
    if (cache != NULL)
    {
    	cache->maps[ix][iy] = height_map;
#ifdef TERRAIN_DEBUG_CACHE_HITS
        iDebugCacheQueries++;
        if (iDebugCacheQueries == 10000)
        {
            printf("CACHE HIT RATE %0.02f\n", iDebugCacheHits/100.0f);
            iDebugCacheHits = iDebugCacheQueries = 0;
        }
#endif
    }
    
    return height_map;
}

HeightMap* terrainGetHeightMapForLocalGridPos(ZoneMapLayer *layer, IVec2 local_pos)
{
    // TomY TODO: Search trackers
    return NULL;
}

bool heightMapSaveSource(HeightMap *height_map, TerrainFileList *list, IVec2 local_pos, IVec3 min_pos, int index, int lod)
{
   	U32 size, i, j;
    U32 out_size;
    unsigned char *data;
    TerrainBuffer *height_buffer;
	TerrainBuffer *alpha_buffer;
	TerrainBuffer *material_buffer;
	TerrainBuffer *soil_depth_buffer;
	TerrainBuffer *object_buffer;

    size = height_map->size;

    // Save heights
	if (list->heightmap.file)
    {
        F32 *height_buf = calloc(1, sizeof(F32)*size*size);
        height_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
        assert(height_buffer);

        height_buf[0] = height_buffer->data_f32[0];
        for (i = 1; i < size*size; ++i)
            height_buf[i] = height_buffer->data_f32[i] - height_buffer->data_f32[i-1];

        data = zipData(height_buf, size*size*4, &out_size);
        if (out_size < size*size*4)
        {
            // Save compressed
            fwrite(&out_size, 1, 4, list->heightmap.file);
            fwrite(data, 1, out_size, list->heightmap.file);
        }
        else
        {
            // Save uncompressed
            out_size = 0;
            fwrite(&out_size, 1, 4, list->heightmap.file);
            fwrite(height_buffer->data_f32, 1, size*size*4, list->heightmap.file);
        }
        SAFE_FREE(data);
        SAFE_FREE(height_buf);

	}

    // Save alphas
	if (list->holemap.file)
    {
        U8 *buf;
        
        alpha_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_ALPHA, height_map->loaded_level_of_detail);

        buf = calloc(1, size*size*SRC_ALPHA_SIZE);

        if (alpha_buffer)
            for (i=0; i<size*size; i++)
                buf[i] = CLAMP(alpha_buffer->data_byte[i], 0, 255);
        else
            memset(buf, 255, size*size*SRC_ALPHA_SIZE);

        data = zipData(buf, size*size*SRC_ALPHA_SIZE, &out_size);
        if (out_size < size*size*SRC_ALPHA_SIZE)
        {
            // Save compressed
            fwrite(&out_size, 1, 4, list->holemap.file);
            fwrite(data, 1, out_size, list->holemap.file);
        }
        else
        {
            // Save uncompressed
            out_size = 0;
            fwrite(&out_size, 1, 4, list->holemap.file);
            fwrite(buf, (size*size*SRC_ALPHA_SIZE), 1, list->holemap.file);
        }
        SAFE_FREE(data);
        SAFE_FREE(buf);    
    }

    // Save materials
    if (list->materialmap.file)
    {
        U8 *buf;

        material_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);
        if(!material_buffer)
            material_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);

        buf = calloc(1, size*size*SRC_MATERIAL_SIZE);

        fwrite(&height_map->material_count, sizeof(U8), 1, list->materialmap.file);
        fwrite(height_map->material_ids, sizeof(U8), NUM_MATERIALS, list->materialmap.file);

        if (material_buffer)
        {
            for (i=0; i<size*size; i++)
            {
                for(j=0; j < NUM_MATERIALS; j++)
                {
                    buf[i*NUM_MATERIALS+j] = (U8)material_buffer->data_material[i].weights[j];
                }
            }
        }

        data = zipData(buf, size*size*SRC_MATERIAL_SIZE, &out_size);
        if (out_size < size*size*SRC_MATERIAL_SIZE)
        {
            // Save compressed
            fwrite(&out_size, 1, 4, list->materialmap.file);
            fwrite(data, 1, out_size, list->materialmap.file);
        }
        else
        {
            // Save uncompressed
            out_size = 0;
            fwrite(&out_size, 1, 4, list->materialmap.file);
            fwrite(buf, (size*size*SRC_MATERIAL_SIZE), 1, list->materialmap.file);
        }
        SAFE_FREE(data);
        SAFE_FREE(buf);
    }

    // Save soil depths
    if (list->soildepthmap.file)
    {
        F32 *buf = NULL;;
        soil_depth_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, height_map->loaded_level_of_detail);
     
        if (soil_depth_buffer)
        {
            buf = soil_depth_buffer->data_f32;
        }
        else
        {
            buf = calloc(1, sizeof(F32)*size*size);
            for (i=0; i<size*size; i++)
            {
                buf[i] = 0.f;
            }
        }
        
        data = zipData(buf, size*size*SRC_SOIL_DEPTH_SIZE, &out_size);
        if (out_size < size*size*SRC_SOIL_DEPTH_SIZE)
        {
            // Save compressed
            fwrite(&out_size, 1, 4, list->soildepthmap.file);
            fwrite(data, 1, out_size, list->soildepthmap.file);
        }
        else
        {
            // Save uncompressed
            out_size = 0;
            fwrite(&out_size, 1, 4, list->soildepthmap.file);
            fwrite(buf, (size*size*SRC_SOIL_DEPTH_SIZE), 1, list->soildepthmap.file);
        }
        SAFE_FREE(data);
        if (!soil_depth_buffer)
            SAFE_FREE(buf);
    }

    // Save objects
    if (list->objectmap.file)
    {
        U8 num_sub_buffers = 0;
        IVec2 map_pos;

        object_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
        if (object_buffer)
            num_sub_buffers = eaSize(&object_buffer->data_objects);

		map_pos[0] = local_pos[0] - min_pos[0];
		map_pos[1] = local_pos[1] - min_pos[2];
        fwrite(&map_pos, sizeof(IVec2), 1, list->objectmap.file);
        fwrite(&num_sub_buffers, sizeof(U8), 1, list->objectmap.file);

        if (num_sub_buffers > 0 && object_buffer)
        {
            U8 *in_data, *in_data_ptr;
            U32 in_data_size = num_sub_buffers*(1+(size*size*sizeof(TerrainObjectDensity)));
            in_data_ptr = in_data = calloc(1, in_data_size);

            for (i = 0; i < eaUSize(&object_buffer->data_objects); i++)
            {
                *in_data_ptr++ = object_buffer->data_objects[i]->object_type;
                memcpy(in_data_ptr, object_buffer->data_objects[i]->density, (size*size*sizeof(TerrainObjectDensity)));
                in_data_ptr += (size*size*sizeof(TerrainObjectDensity));
            }
            
            data = zipData(in_data, in_data_size, &out_size);
            if (out_size < in_data_size)
            {
                // Save compressed
                fwrite(&out_size, 1, 4, list->objectmap.file);
                fwrite(data, 1, out_size, list->objectmap.file);
            }
            else
            {
                // Save uncompressed
                out_size = 0;
                fwrite(&out_size, 1, 4, list->objectmap.file);
                fwrite(in_data, in_data_size, 1, list->objectmap.file);
            }
            SAFE_FREE(data);
            SAFE_FREE(in_data);
        }
    }

	return true;
}


bool heightMapTiffColorSave(StashTable heightmaps, TerrainFile *tfile, IVec3 block_min, U32 lod, U32 block_width, U32 block_height)
{
	int i;
	U32 j, x, y;
	TiffIFD imageFileDir = {0};
	U32 size = GRID_LOD(lod);
	U32 IDF_location;
	U8 *strip;
	
	imageFileDir.width = ((size-1) * block_width) + 1;
	imageFileDir.height = ((size-1) * block_height) + 1;
	imageFileDir.bytesPerChannel = 1;
	imageFileDir.numChannels = 3;
	imageFileDir.lzwCompressed = true;
	imageFileDir.horizontalDifferencingType = TIFF_DIFF_HORIZONTAL;
	imageFileDir.rows_per_strip = (size-1);
	imageFileDir.strip_count = block_height + 1;
	
	imageFileDir.strip_offsets = calloc(imageFileDir.strip_count, sizeof(U32));
	imageFileDir.strip_byte_counts = calloc(imageFileDir.strip_count, sizeof(U32));

	//Fill with 0 for now, will put correct offset in later
	tiffWriteHeader(tfile->file, 0, 0);
	
	//For each strip
	strip = calloc(imageFileDir.width * (imageFileDir.rows_per_strip+1),  sizeof(U8)*3);
	for(j=0; j < imageFileDir.strip_count-1; j++)
	{
		//Save strip location
		imageFileDir.strip_offsets[j] = ftell(tfile->file);

		//Fill strip data
		//Reverse order so that horizontal differencing works
		for(i=block_width-1; i >= 0 ; i--)
		{
			HeightMap *height_map = NULL;
			TerrainBuffer *color_buffer;
			IVec2 local_pos;
	
			local_pos[0] = block_min[0] + i;
			local_pos[1] = block_min[2] + (imageFileDir.strip_count-2 - j);//Tiff is Y flipped
			assert(stashIntFindPointer(heightmaps, getTerrainGridPosKey(local_pos), &height_map));

			color_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, lod);
			
			for(x=0; x < size; x++)
			{
				int sx = x + i*(size-1);
				for(y=0; y < size; y++)
				{
					int rev_y = size-1 - y;//Tiff is Y flipped

					if(x != 0)
					{
						//horizontal differencing
						strip[(sx + y*imageFileDir.width)*3 + 0] =  color_buffer->data_u8vec3[x + rev_y*color_buffer->size][2] -
																	color_buffer->data_u8vec3[(x-1) + rev_y*color_buffer->size][2];
						strip[(sx + y*imageFileDir.width)*3 + 1] =  color_buffer->data_u8vec3[x + rev_y*color_buffer->size][1] -
																	color_buffer->data_u8vec3[(x-1) + rev_y*color_buffer->size][1];
						strip[(sx + y*imageFileDir.width)*3 + 2] =  color_buffer->data_u8vec3[x + rev_y*color_buffer->size][0] -
																	color_buffer->data_u8vec3[(x-1) + rev_y*color_buffer->size][0];
					
					}
					else
					{
						strip[(sx + y*imageFileDir.width)*3 + 0] = color_buffer->data_u8vec3[x + rev_y*color_buffer->size][2];
						strip[(sx + y*imageFileDir.width)*3 + 1] = color_buffer->data_u8vec3[x + rev_y*color_buffer->size][1];
						strip[(sx + y*imageFileDir.width)*3 + 2] = color_buffer->data_u8vec3[x + rev_y*color_buffer->size][0];
					}
				}
			}			
			
		}

		//Compress and Write Data
		imageFileDir.strip_byte_counts[j] = tiffWriteStrip(tfile->file, &imageFileDir, strip, imageFileDir.rows_per_strip);
	}
	//Compress and Write the final strip
	imageFileDir.strip_offsets[j] = ftell(tfile->file);
	imageFileDir.strip_byte_counts[j] = tiffWriteStrip(tfile->file, &imageFileDir, strip + imageFileDir.rows_per_strip*imageFileDir.width*3*sizeof(U8), 1);
	free(strip);

	//Write out the Image File Directory
	IDF_location = ftell(tfile->file);
	tiffWriteIFD(tfile->file, &imageFileDir, IDF_location);

	//Write the real header
	tiffWriteHeader(tfile->file, IDF_location, 0);

	free(imageFileDir.strip_offsets);
	free(imageFileDir.strip_byte_counts);

	return true;
}

bool heightMapSaveOcclusionSource(HeightMap *height_map, TerrainFileList *list, IVec2 local_pos)
{
    TerrainBuffer *occlusion_buffer;
    char empty_buf[16] = { 0 };
    if (!list->heightmap.file)
        return false;
    if (!height_map)
    {
        fwrite(empty_buf, 1, 16, list->heightmap.file);
        return true;
    }
    occlusion_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OCCLUSION, 5);
    if (occlusion_buffer)
    {
        fwrite(occlusion_buffer->data_byte, 1, 16, list->heightmap.file);
    }
    else
    {
        char bytes[16] = { 0 };
        fwrite(bytes, 1, 16, list->heightmap.file);
    }
    return true;
}

void closeTerrainFile(TerrainFile *file)
{
	if (file->file)
	{
		fclose(file->file);
		file->file = NULL;
	}
}

void closeFiles(TerrainFileList *list)
{
	closeTerrainFile(&list->heightmap);
	closeTerrainFile(&list->holemap);
	closeTerrainFile(&list->materialmap);
	closeTerrainFile(&list->soildepthmap);
	closeTerrainFile(&list->objectmap);
	closeTerrainFile(&list->tiffcolormap);
}

bool terrainBlockSaveSource(TerrainBlockRange *block, int blocknum, int loaded_lod, StashTable heightmaps)
{
    IVec2 local_pos;
    U32 index = 0;
    U32 size;
    U32 block_width, block_height, num_blocks;
    static char buf[1024];
    TerrainFileList *list = &block->source_files;
    U32 lod_flag = loaded_lod | TERRAIN_SOURCE_COMPRESSED;

    printf("Saving block %d, lod %d\n", blocknum, loaded_lod);

    block_width = block->range.max_block[0]+1-block->range.min_block[0];
    block_height = block->range.max_block[2]+1-block->range.min_block[2];
    num_blocks = block_width * block_height;

    // Make Backups
    fileMakeLocalBackup(list->heightmap.file_base.fullname, BACKUP_TIME_TO_KEEP);
    fileMakeLocalBackup(list->holemap.file_base.fullname, BACKUP_TIME_TO_KEEP);
    fileMakeLocalBackup(list->tiffcolormap.file_base.fullname, BACKUP_TIME_TO_KEEP);
    fileMakeLocalBackup(list->ecomap.file_base.fullname, BACKUP_TIME_TO_KEEP);
    fileMakeLocalBackup(list->materialmap.file_base.fullname, BACKUP_TIME_TO_KEEP);
    fileMakeLocalBackup(list->soildepthmap.file_base.fullname, BACKUP_TIME_TO_KEEP);
    fileMakeLocalBackup(list->objectmap.file_base.fullname, BACKUP_TIME_TO_KEEP);

    // Open files
    if (!openTerrainFileWriteSource(&list->heightmap, true) ||
        !openTerrainFileWriteSource(&list->holemap, true) ||
        !openTerrainFileWriteSource(&list->tiffcolormap, true) ||
        !openTerrainFileWriteSource(&list->materialmap, true) ||
        !openTerrainFileWriteSource(&list->soildepthmap, true) ||
        !openTerrainFileWriteSource(&list->objectmap, true))
    {
        Errorf("Error opening terrain source files! Your files may be corrupted.\n");
        closeFiles(list);
        return false;
    }

    memset(buf, 0, 1024);

    // Headers
    fwrite(&block_width, sizeof(U32), 1, list->heightmap.file);
    fwrite(&block_height, sizeof(U32), 1, list->heightmap.file);
    fwrite(&lod_flag, sizeof(U32), 1, list->heightmap.file);

    fwrite(&block_width, sizeof(U32), 1, list->holemap.file);
    fwrite(&block_height, sizeof(U32), 1, list->holemap.file);
    fwrite(&lod_flag, sizeof(U32), 1, list->holemap.file);
            
    fwrite(&block_width, sizeof(U32), 1, list->materialmap.file);
    fwrite(&block_height, sizeof(U32), 1, list->materialmap.file);
    fwrite(&lod_flag, sizeof(U32), 1, list->materialmap.file);

    fwrite(&block_width, sizeof(U32), 1, list->soildepthmap.file);
    fwrite(&block_height, sizeof(U32), 1, list->soildepthmap.file);
    fwrite(&lod_flag, sizeof(U32), 1, list->soildepthmap.file);

    fwrite(&num_blocks, sizeof(U32), 1, list->objectmap.file);
    fwrite(&lod_flag, sizeof(U32), 1, list->objectmap.file);

    size = GRID_LOD(loaded_lod);

    // Save into files
    for (local_pos[1] = block->range.min_block[2]; local_pos[1] <= block->range.max_block[2]; ++local_pos[1])
    {
        for (local_pos[0] = block->range.min_block[0]; local_pos[0] <= block->range.max_block[0]; ++local_pos[0])
        {
            bool free_height_map = false;
            HeightMap *height_map = NULL;
            stashIntFindPointer(heightmaps, getTerrainGridPosKey(local_pos), &height_map);
            if (!height_map) {
                height_map = heightMapCreateDefault(NULL, loaded_lod, 0, 0);
                free_height_map = true;
            }

            heightMapSaveSource(height_map, list, local_pos, block->range.min_block, index++, loaded_lod);

            if (free_height_map)
                heightMapDestroy(height_map);    
        }
    }

    //Save Color separate because it is saved completely different
    heightMapTiffColorSave(heightmaps, &list->tiffcolormap, block->range.min_block, GET_COLOR_LOD(loaded_lod), block_width, block_height);

    for (local_pos[1] = block->range.min_block[2]; local_pos[1] <= block->range.max_block[2]; ++local_pos[1])
    {
        for (local_pos[0] = block->range.min_block[0]; local_pos[0] <= block->range.max_block[0]; ++local_pos[0])
        {
            HeightMap *height_map = NULL;
            stashIntFindPointer(heightmaps, getTerrainGridPosKey(local_pos), &height_map);
            heightMapSaveOcclusionSource(height_map, list, local_pos);
        }
    }
            
    closeFiles(list);

    printf("Done saving.\n");

    return true;
}

void terrainBlockValidateError(ErrorMessage *errMsg, void *userdata)
{
	// TomY TODO - log errors here
}

bool terrainBlockValidateSource(ZoneMapLayer *layer, TerrainBlockRange *block)
{
	HeightMap **cache = NULL;
	bool ret = true;
	LibFileLoad *layerondisk = NULL; // loadLayerFromDisk(layer->filename); // TomY Can't do this from a background thread; figure out something later
	ErrorfPushCallback(terrainBlockValidateError, NULL);
	if (!terrainBlockDoLoadSource(layer, block, block->range.min_block, block->range.max_block, &cache, false, true, layerondisk))
	{
		ret = false;
	}
	StructDestroy(parse_LibFileLoad, layerondisk);
	eaDestroyEx(&cache, heightMapDestroy);
	ErrorfPopCallback();
	return ret;
}

bool terrainBlockCommitSource(ZoneMapLayer *layer, TerrainBlockRange *block)
{
	bool failure = false;
	TerrainFileList *list = &block->source_files;
	if (!commitTerrainFileSource(&list->heightmap))
		failure = true;
	if (!commitTerrainFileSource(&list->holemap))
		failure = true;
	if (!commitTerrainFileSource(&list->colormap) &&
		!commitTerrainFileSource(&list->tiffcolormap))
		failure = true;
	if (!commitTerrainFileSource(&list->materialmap))
		failure = true;
	if (!commitTerrainFileSource(&list->objectmap))
		failure = true;
	if (!commitTerrainFileSource(&list->soildepthmap))
		failure = true;
	return !failure;
}

void heightMapRearrangeMaterialWeights(HeightMap *height_map, TerrainBuffer *material_buffer, U8 old_mats[NUM_MATERIALS], U8 old_mat_count, U8 new_mats[NUM_MATERIALS], U8 new_mat_count)
{
	int i,j,z,x;
	int new_indices[NUM_MATERIALS] = {0};
	bool can_skip = true;

    assert(old_mat_count == height_map->material_count);
    for (i = 0; i < old_mat_count; i++)
        assert(height_map->material_ids[i] == old_mats[i]);

	//Find the new indices for out material wieghts
	for(i=0; i<new_mat_count; i++)
	{
		for(j=0; j<old_mat_count; j++)
		{
			if(new_mats[i] == old_mats[j])
			{
				new_indices[i] = j;
				if(i != j)
					can_skip = false;
				break;
			}
		}
		//If was not found then mark as such
		if(j == old_mat_count)
		{
			new_indices[i] = -1;
			can_skip = false;		
		}
	}
	for(i=new_mat_count; i<NUM_MATERIALS; i++)
	{
		new_indices[i] = -1;
	}

	//If all the weights are the same then just skip 
	if (!can_skip)
    {
        //For each point
        for(z=0; z < material_buffer->size; z++)
        {
            for(x=0; x < material_buffer->size; x++)
            {
                TerrainMaterialWeight old_mat_weights = material_buffer->data_material[x + z*material_buffer->size];
                //Rearange values
                for(i=0; i<NUM_MATERIALS; i++)
                {
                    if(new_indices[i] >= 0)
                        material_buffer->data_material[x + z*material_buffer->size].weights[i] = old_mat_weights.weights[new_indices[i]];
                    else
                        material_buffer->data_material[x + z*material_buffer->size].weights[i] = 0.;
                }
            }
        }
    }

    memcpy(height_map->material_ids, new_mats, sizeof(U8)*NUM_MATERIALS);
    height_map->material_count = new_mat_count;
}

void heightMapSaveBackupBuffers(HeightMap *height_map, HeightMapBackupList **map_undo_list)
{
	int i;
	HeightMapBackup *backup = NULL;

	for(i=0; i < TERRAIN_BUFFER_NUM_TYPES; i++)
	{
		//If there is a backup buffer 
		if(height_map->backup_buffer[i] != NULL)
		{
			//Make a list of backup buffers if not already made
			if(backup == NULL)
			{
				backup = malloc(sizeof(HeightMapBackup));
				backup->height_map = height_map;
				backup->backup_buffers = NULL;
				//If there is no undo list made yet, make it
				if((*map_undo_list) == NULL)
					(*map_undo_list) = calloc(1, sizeof(HeightMapBackupList));
				eaPush(&(*map_undo_list)->list, backup);
			}
			//Add to the list
			eaPush(&backup->backup_buffers, height_map->backup_buffer[i]);
		}
		//Remove backup buffer from map
		height_map->backup_buffer[i] = NULL;
    }
}

void heightMapApplyBackupBuffer(HeightMap *height_map, TerrainBuffer *buffer)
{
    int m, n;
    TerrainBuffer *exist_buffer = heightMapGetBuffer(height_map, buffer->type, buffer->lod);
    assert(exist_buffer);
    if (buffer->type == TERRAIN_BUFFER_OBJECTS)
    {
        int map_sub_buffer_cnt = eaSize(&exist_buffer->data_objects);
        bool *sub_buffer_found = ScratchAlloc(map_sub_buffer_cnt*sizeof(bool));

        for(m=0; m < eaSize(&buffer->data_objects); m++)
        {
            TerrainObjectBuffer *backup_data = buffer->data_objects[m];
            bool found = false;
            for (n=0; n < map_sub_buffer_cnt; n++)
            {
                if(exist_buffer->data_objects[n]->object_type == backup_data->object_type)
                {
                    TerrainObjectDensity *temp = backup_data->density;
                    sub_buffer_found[n] = true;
                    //If not drawn on, skip
                    if(temp)
                    {
                        backup_data->density = exist_buffer->data_objects[n]->density;
                        exist_buffer->data_objects[n]->density = temp;
                        heightMapUpdateVisualization(height_map);
                        height_map->buffer_touched[TERRAIN_BUFFER_OBJECTS] = true;
                    }
                    found = true;
                    break;
                }
            }
            //If sub buffer on the map is not found, move it to the map
            if(!found)
            {
                TerrainObjectBuffer *sub_buffer = eaRemoveFast(&buffer->data_objects, m);
                assert(sub_buffer->density);
                eaPush(&exist_buffer->data_objects, sub_buffer); 
                heightMapUpdateVisualization(height_map);
                height_map->buffer_touched[TERRAIN_BUFFER_OBJECTS] = true;
                m--;
            }
        }
        //If there was an buffer on the map and not on the undo, move it to the undo
        for(n=0; n < map_sub_buffer_cnt; n++)
        {
            if(sub_buffer_found[n] == false)
            {
                TerrainObjectBuffer *sub_buffer = eaRemoveFast(&exist_buffer->data_objects, n);
                assert(sub_buffer->density);
                eaPush(&buffer->data_objects, sub_buffer);
                heightMapUpdateVisualization(height_map);
                height_map->buffer_touched[TERRAIN_BUFFER_OBJECTS] = true;
                map_sub_buffer_cnt--;
                n--;
            }
        }
        ScratchFree(sub_buffer_found);
    }
    else
    {
        //Swap Buffers
        U8 *data = exist_buffer->data_byte;
        exist_buffer->data_byte = buffer->data_byte;
        buffer->data_byte = data;
		if (buffer->type == TERRAIN_BUFFER_HEIGHT || buffer->type == TERRAIN_BUFFER_MATERIAL || buffer->type == TERRAIN_BUFFER_ALPHA)
			heightMapUpdateGeometry(height_map);
       else if (buffer->type == TERRAIN_BUFFER_COLOR)
            heightMapUpdateTextures(height_map);
        else
            heightMapUpdateVisualization(height_map);
        height_map->height_time++;
        assert(buffer->type < TERRAIN_BUFFER_NUM_TYPES);
        height_map->buffer_touched[buffer->type] = true;
    }
}


void DestroyHeightMapBackup(HeightMapBackup *map_backup)
{
	eaDestroyEx(&map_backup->backup_buffers, DestroyTerrainBuffer);
    SAFE_FREE(map_backup);
}

void heightMapFreeBackupBuffers(HeightMapBackupList *map_backup_list)
{
	eaDestroyEx(&map_backup_list->list, DestroyHeightMapBackup);
    SAFE_FREE(map_backup_list);
}

void heightMapFindUnusedObjects(HeightMap *height_map)
{
	int i, j, k;
	TerrainBuffer *object_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
	TerrainBuffer *backup_buffer = height_map->backup_buffer[TERRAIN_BUFFER_OBJECTS];
	
	if(backup_buffer && object_buffer)
	{
		//Look through the objects that were previously on the map
		for(i=0; i < eaSize(&backup_buffer->data_objects); i++)
		{
			//If something was changed
			if(backup_buffer->data_objects[i]->density)
			{
				//Find the object buffer on the current map
				for(j=0; j < eaSize(&object_buffer->data_objects); j++)
				{
					if(object_buffer->data_objects[j]->object_type == backup_buffer->data_objects[i]->object_type)
					{
						bool unused = true;
						//Search to see if all are set to 0
						for(k=0; k < object_buffer->size*object_buffer->size; k++)
						{
							if(object_buffer->data_objects[j]->density[k] != 0)
							{
								unused = false;
								break;
							}
						}
						//If not used any more then remove
						if(unused)
						{
							TerrainObjectBuffer *sub_buffer = eaRemove(&object_buffer->data_objects, j);
							free(sub_buffer->density);
							free(sub_buffer);
						}
						break;
					}
				}
				
			}
		}
	}
}

void heightMapFindUnusedMaterials(HeightMap *height_map, TerrainMaterialChangeList **mat_undo_list)
{
	int x,z,i;
	TerrainMaterialChange *mat_change;
	TerrainBuffer *material_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);
	bool used_materials[NUM_MATERIALS]={0};
	bool all_mats_used;

	if(material_buffer)
	{
		for(z=0; z < material_buffer->size; z++)
		{
			for(x=0; x < material_buffer->size; x++)
			{
				TerrainMaterialWeight mat_weights = material_buffer->data_material[x + z*material_buffer->size];
				all_mats_used = true;
				
				//For each of the materials
				for(i=0; i < height_map->material_count; i++)
				{
					//If we have not found a point that uses the material
					if(used_materials[i] == false)
					{
						//If we are not useing the material on this point then keep searching
						if(mat_weights.weights[i] == 0.)
							all_mats_used = false;
						//Otherwise mark as having found a point that uses this material
						else
							used_materials[i] = true;
					}
				}

				//If all the materials are being used stop searching
				if(all_mats_used)
					return;
			}
		}

		//If not all the materials were being used then remove the ones that are not being used
		mat_change = calloc(1, sizeof(TerrainMaterialChange));
		mat_change->height_map = height_map;

		//Save old values
		memcpy(mat_change->old_material_ids, height_map->material_ids, sizeof(U8)*NUM_MATERIALS);
		mat_change->old_material_count = height_map->material_count;

        // Fill in the new values
        memset(mat_change->new_material_ids, UNDEFINED_MAT, NUM_MATERIALS);
		mat_change->new_material_count = 0;
        
		for(i=0; i < mat_change->old_material_count; i++)
		{
			if(used_materials[i] == true)
				mat_change->new_material_ids[mat_change->new_material_count++] = height_map->material_ids[i];
		}

		heightMapRearrangeMaterialWeights(height_map, material_buffer,
                                          mat_change->old_material_ids, mat_change->old_material_count,
                                          mat_change->new_material_ids, mat_change->new_material_count);
        
		if((*mat_undo_list) == NULL)
			(*mat_undo_list) = calloc(1, sizeof(TerrainMaterialChangeList));
		eaPush(&(*mat_undo_list)->list, mat_change);
    }
}

void terrainSubdivObjectDensity(TerrainObjectDensity *buffer_data, int stride, int size_x, int size_y )
{
	int i, j;

	// face vertices
	for (j = stride; j < size_y; j += 2*stride)
	{
		for (i = stride; i < size_x; i += 2*stride)
		{
			U32 sum = 0;
			sum += buffer_data[i-stride + (j-stride)*size_x];
			sum += buffer_data[i+stride + (j-stride)*size_x];
			sum += buffer_data[i+stride + (j+stride)*size_x];
			sum += buffer_data[i-stride + (j+stride)*size_x];
			sum++;
			sum /= 4;

			buffer_data[i + j*size_x] = sum;
		}
	}
	// edge vertices
	// horizontal edges
	for (j = 0; j < size_y; j += 2*stride)
	{
		for (i = stride; i < size_x; i += 2*stride)
		{
			U32 sum = 0;
			sum += buffer_data[i-stride + j*size_x];
			sum += buffer_data[i+stride + j*size_x];
			sum++;
			sum /= 2;

			buffer_data[i + j*size_x] = sum;
		}
	}
	// vertical edges
	for (j = stride; j < size_y; j += 2*stride)
	{
		for (i = 0; i < size_x; i += 2*stride)
		{
			U32 sum = 0;
			sum += buffer_data[i + (j-stride)*size_x];
			sum += buffer_data[i + (j+stride)*size_x];
			sum++;
			sum /= 2;

			buffer_data[i + j*size_x] = sum;
		}
	}
}

static void terrainSubdivU8Vec3( TerrainBuffer *buffer, int stride )
{
	int i, j;
	int buf_size = ((buffer->size % 2) == 0) ? buffer->size-3 : buffer->size;

	// face vertices
	for (j = stride; j < buf_size; j += 2*stride)
	{
		for (i = stride; i < buf_size; i += 2*stride)
		{
			Vec3 sum;
			setVec3same(sum, 0);
			addToVec3(buffer->data_u8vec3[i-stride + (j-stride)*buffer->size], sum);
			addToVec3(buffer->data_u8vec3[i+stride + (j-stride)*buffer->size], sum);
			addToVec3(buffer->data_u8vec3[i+stride + (j+stride)*buffer->size], sum);
			addToVec3(buffer->data_u8vec3[i-stride + (j+stride)*buffer->size], sum);
			scaleByVec3(sum,0.25);
			addVec3same(sum, 0.5, buffer->data_u8vec3[i + j*buffer->size]);
		}
	}
	// edge vertices
	// horizontal edges
	for (j = 0; j < buf_size; j += 2*stride)
	{
		for (i = stride; i < buf_size; i += 2*stride)
		{
			Vec3 sum;
			setVec3same(sum, 0);
			addToVec3(buffer->data_u8vec3[i-stride + j*buffer->size], sum);
			addToVec3(buffer->data_u8vec3[i+stride + j*buffer->size], sum);
			scaleByVec3(sum,0.5);
			addVec3same(sum, 0.5, buffer->data_u8vec3[i + j*buffer->size]);
		}
	}
	// vertical edges
	for (j = stride; j < buf_size; j += 2*stride)
	{
		for (i = 0; i < buf_size; i += 2*stride)
		{
			Vec3 sum;
			setVec3same(sum, 0);
			addToVec3(buffer->data_u8vec3[i + (j-stride)*buffer->size], sum);
			addToVec3(buffer->data_u8vec3[i + (j+stride)*buffer->size], sum);
			scaleByVec3(sum,0.5);
			addVec3same(sum, 0.5, buffer->data_u8vec3[i + j*buffer->size]);
		}
	}
}


static void terrainSubdivAttr( TerrainBuffer *buffer, int stride )
{
	int i, j;

	// face vertices
	for (j = stride; j < buffer->size; j += 2*stride)
	{
		for (i = stride; i < buffer->size; i += 2*stride)
		{
			if(buffer->data_byte[i+stride + (j-stride)*buffer->size] == buffer->data_byte[i+stride + (j+stride)*buffer->size] &&
				buffer->data_byte[i-stride + (j-stride)*buffer->size] != buffer->data_byte[i-stride + (j+stride)*buffer->size])
			{
				buffer->data_byte[i + j*buffer->size] = buffer->data_byte[i+stride + (j-stride)*buffer->size];
			}
			else if(buffer->data_byte[i+stride + (j+stride)*buffer->size] == buffer->data_byte[i-stride + (j+stride)*buffer->size] &&
					buffer->data_byte[i-stride + (j-stride)*buffer->size] != buffer->data_byte[i+stride + (j-stride)*buffer->size])
			{
				buffer->data_byte[i + j*buffer->size] = buffer->data_byte[i+stride + (j+stride)*buffer->size];
			}
			else
			{
				buffer->data_byte[i + j*buffer->size] = buffer->data_byte[i-stride + (j-stride)*buffer->size];
			}
		}
	}
	// edge vertices
	// horizontal edges
	for (j = 0; j < buffer->size; j += 2*stride)
	{
		for (i = stride; i < buffer->size; i += 2*stride)
		{
			buffer->data_byte[i + j*buffer->size] = buffer->data_byte[i-stride + j*buffer->size];
		}
	}
	// vertical edges
	for (j = stride; j < buffer->size; j += 2*stride)
	{
		for (i = 0; i < buffer->size; i += 2*stride)
		{
			buffer->data_byte[i + j*buffer->size] = buffer->data_byte[i + (j+stride)*buffer->size];
		}
	}
}

static void terrainSubdivAlpha( TerrainBuffer *buffer, int stride )
{
	int i, j;

	// face vertices
	for (j = stride; j < buffer->size; j += 2*stride)
	{
		for (i = stride; i < buffer->size; i += 2*stride)
		{	
			if(	buffer->data_byte[i-stride + (j-stride)*buffer->size] == 0 ||
				buffer->data_byte[i+stride + (j-stride)*buffer->size] == 0 ||
				buffer->data_byte[i+stride + (j+stride)*buffer->size] == 0 ||
				buffer->data_byte[i-stride + (j+stride)*buffer->size] == 0	)
			{
				buffer->data_byte[i + j*buffer->size] = 0;
			}
			else
			{
				buffer->data_byte[i + j*buffer->size] = 255;			
			}
		}
	}
	// edge vertices
	// horizontal edges
	for (j = 0; j < buffer->size; j += 2*stride)
	{
		for (i = stride; i < buffer->size; i += 2*stride)
		{
			if(	buffer->data_byte[i-stride + j*buffer->size] == 0 ||
				buffer->data_byte[i+stride + j*buffer->size] == 0	)
			{
				buffer->data_byte[i + j*buffer->size] = 0;
			}
			else
			{
				buffer->data_byte[i + j*buffer->size] = 255;			
			}
		}
	}
	// vertical edges
	for (j = stride; j < buffer->size; j += 2*stride)
	{
		for (i = 0; i < buffer->size; i += 2*stride)
		{
			if(	buffer->data_byte[i + (j-stride)*buffer->size] == 0 ||
				buffer->data_byte[i + (j+stride)*buffer->size] == 0	)
			{
				buffer->data_byte[i + j*buffer->size] = 0;
			}
			else
			{
				buffer->data_byte[i + j*buffer->size] = 255;			
			}
		}
	}
}

__forceinline static void accumulateMaterialWeights(TerrainMaterialWeight src, TerrainMaterialWeight *dest)
{
	int i;
	for(i=0; i < NUM_MATERIALS; i++)
	{
		dest->weights[i] += src.weights[i];
	}
}

__forceinline static void scaleMaterialWeights(TerrainMaterialWeight *dest, F32 val)
{
	int i;
	for(i=0; i < NUM_MATERIALS; i++)
	{
		dest->weights[i] *= val;
	}
}

__forceinline static void copyMaterialWeights(TerrainMaterialWeight src, TerrainMaterialWeight *dest)
{
	int i;
	for(i=0; i < NUM_MATERIALS; i++)
	{
		dest->weights[i] = src.weights[i];
	}
}

static void terrainSubdivMaterialWeights(TerrainBuffer *buffer, int stride)
{
	int i, j;
	// face vertices
	for (j = stride; j < buffer->size; j += 2*stride)
	{
		for (i = stride; i < buffer->size; i += 2*stride)
		{
			TerrainMaterialWeight sum = {0};

			accumulateMaterialWeights(buffer->data_material[i-stride + (j-stride)*buffer->size], &sum);
			accumulateMaterialWeights(buffer->data_material[i-stride + (j+stride)*buffer->size], &sum);
			accumulateMaterialWeights(buffer->data_material[i+stride + (j+stride)*buffer->size], &sum);
			accumulateMaterialWeights(buffer->data_material[i+stride + (j-stride)*buffer->size], &sum);

			scaleMaterialWeights(&sum, 0.25f);
			copyMaterialWeights(sum, &buffer->data_material[i + j*buffer->size]);
		}
	}
	// edge vertices
	// horizontal edges
	for (j = 0; j < buffer->size; j += 2*stride)
	{
		for (i = stride; i < buffer->size; i += 2*stride)
		{
			TerrainMaterialWeight sum = {0};

			accumulateMaterialWeights(buffer->data_material[i-stride + j*buffer->size], &sum);
			accumulateMaterialWeights(buffer->data_material[i+stride + j*buffer->size], &sum);

			scaleMaterialWeights(&sum, 0.5f);
			copyMaterialWeights(sum, &buffer->data_material[i + j*buffer->size]);
		}
	}
	// vertical edges
	for (j = stride; j < buffer->size; j += 2*stride)
	{
		for (i = 0; i < buffer->size; i += 2*stride)
		{
			TerrainMaterialWeight sum = {0};

			accumulateMaterialWeights(buffer->data_material[i + (j-stride)*buffer->size], &sum);
			accumulateMaterialWeights(buffer->data_material[i + (j+stride)*buffer->size], &sum);
			scaleMaterialWeights(&sum, 0.5f);

			copyMaterialWeights(sum, &buffer->data_material[i + j*buffer->size]);
		}
	}
}

static void terrainSubdivF32( TerrainBuffer *buffer, int stride )
{
	int i, j;

	// face vertices
	for (j = stride; j < buffer->size; j += 2*stride)
	{
		for (i = stride; i < buffer->size; i += 2*stride)
		{
			F32 sum = 0;
			sum += buffer->data_f32[i-stride + (j-stride)*buffer->size];
			sum += buffer->data_f32[i+stride + (j-stride)*buffer->size];
			sum += buffer->data_f32[i+stride + (j+stride)*buffer->size];
			sum += buffer->data_f32[i-stride + (j+stride)*buffer->size];
			sum /= 4;

			buffer->data_f32[i + j*buffer->size] = sum;
		}
	}
	// edge vertices
	// horizontal edges
	for (j = 0; j < buffer->size; j += 2*stride)
	{
		for (i = stride; i < buffer->size; i += 2*stride)
		{
			F32 sum = 0;
			sum += buffer->data_f32[i-stride + j*buffer->size];
			sum += buffer->data_f32[i+stride + j*buffer->size];
			sum /= 2;

			buffer->data_f32[i + j*buffer->size] = sum;
		}
	}
	// vertical edges
	for (j = stride; j < buffer->size; j += 2*stride)
	{
		for (i = 0; i < buffer->size; i += 2*stride)
		{
			F32 sum = 0;
			sum += buffer->data_f32[i + (j-stride)*buffer->size];
			sum += buffer->data_f32[i + (j+stride)*buffer->size];
			sum /= 2;

			buffer->data_f32[i + j*buffer->size] = sum;
		}
	}
}

void heightMapSubDivBuffer(HeightMap *height_map, int type, TerrainBuffer *backup_buffer, U8 *touch_buffer, int draw_lod, bool keep_high_res)
{
	int i, j, x, y;
	int loaded_lod = (type == TERRAIN_BUFFER_COLOR) ? GET_COLOR_LOD(height_map->loaded_level_of_detail) : height_map->loaded_level_of_detail;
	TerrainBuffer *buffer = heightMapGetBuffer(height_map, type, loaded_lod);
	int lod_diff = draw_lod - loaded_lod;
	int touch_buffer_size = GRID_LOD(draw_lod);
	int color_lod_diff;

	if(!buffer)
		return;

	switch(type)
	{
	case TERRAIN_BUFFER_SOIL_DEPTH:
	case TERRAIN_BUFFER_HEIGHT:
		if(	type == TERRAIN_BUFFER_HEIGHT && 
			keep_high_res	)
		{
			//Calc Diffs
			for(x=0 ; x < buffer->size; x++)
			{					
				for(y=0 ; y < buffer->size; y++)
				{
					buffer->data_f32[x+y*buffer->size] = buffer->data_f32[x+y*buffer->size] - 
															backup_buffer->data_f32[x+y*backup_buffer->size];
				}
			}
		}
		for(j=lod_diff; j > 0; j--)
		{
			terrainSubdivF32(buffer, 1<<(j-1));
		}
		if(	type == TERRAIN_BUFFER_HEIGHT && 
			keep_high_res	)
		{
			//Change Back to Heights instead of diffs
			for(x=0 ; x < buffer->size; x++)
			{					
				for(y=0 ; y < buffer->size; y++)
				{
					buffer->data_f32[x+y*buffer->size] = buffer->data_f32[x+y*buffer->size] + 
															backup_buffer->data_f32[x+y*backup_buffer->size];
				}
			}
		}
		//Copy in old data for data not touched
		for(x=0 ; x < buffer->size; x++)
		{					
			for(y=0 ; y < buffer->size; y++)
			{
				int sxU, syU, sxD, syD;
				sxU = ((x-1)>>lod_diff)+1;	//Rounded Up
				syU = ((y-1)>>lod_diff)+1;
				sxD = x>>lod_diff;			//Rounded Down
				syD = y>>lod_diff;

				if(	touch_buffer[sxD + syD*touch_buffer_size] ||
					touch_buffer[sxU + syD*touch_buffer_size] ||
					touch_buffer[sxD + syU*touch_buffer_size] ||
					touch_buffer[sxU + syU*touch_buffer_size] )
					continue;

				buffer->data_f32[x+y*buffer->size] = backup_buffer->data_f32[x+y*backup_buffer->size];
			}
		}
		return;
	case TERRAIN_BUFFER_ALPHA:
		//Alpha is only drawn at highest LOD
		return;
	case TERRAIN_BUFFER_MATERIAL:
		for(j=lod_diff; j > 0; j--)
		{
			terrainSubdivMaterialWeights(buffer, 1<<(j-1));
		}
		//Copy in old data for data not touched
		for(x=0 ; x < buffer->size; x++)
		{					
			for(y=0 ; y < buffer->size; y++)
			{
				int sxU, syU, sxD, syD;
				sxU = ((x-1)>>lod_diff)+1;	//Rounded Up
				syU = ((y-1)>>lod_diff)+1;
				sxD = x>>lod_diff;			//Rounded Down
				syD = y>>lod_diff;

				if(	touch_buffer[sxD + syD*touch_buffer_size] ||
					touch_buffer[sxU + syD*touch_buffer_size] ||
					touch_buffer[sxD + syU*touch_buffer_size] ||
					touch_buffer[sxU + syU*touch_buffer_size] )
					continue;

				for(i=0; i < NUM_MATERIALS; i++)
				{
					buffer->data_material[x+y*buffer->size].weights[i] = 
						backup_buffer->data_material[x+y*backup_buffer->size].weights[i];
				}
			}
		}
		return;
	case TERRAIN_BUFFER_COLOR:
		//Lod is actually different for color
		color_lod_diff = GET_COLOR_LOD(draw_lod) - loaded_lod;

		//Copy low res data to the high res buffer
		if(color_lod_diff > 0)
		{
			TerrainBuffer *low_res_buffer = heightMapGetBuffer(height_map, type, GET_COLOR_LOD(draw_lod));
			if(!low_res_buffer)
				break;
			for(x=0; x < low_res_buffer->size; x++)
			{
				for(y=0; y < low_res_buffer->size; y++)
				{
					copyVec3(low_res_buffer->data_u8vec3[x+y*low_res_buffer->size], buffer->data_u8vec3[(x<<color_lod_diff) + (y<<color_lod_diff)*buffer->size]);
				}
			}
		}
		for(j=color_lod_diff; j > 0; j--)
		{
			terrainSubdivU8Vec3(buffer, 1<<(j-1));
		}
		//Copy in old data for data not touched
		for(x=0 ; x < buffer->size; x++)
		{					
			for(y=0 ; y < buffer->size; y++)
			{
				int sxU, syU, sxD, syD;
				sxU = ((x-1)>>lod_diff)+1;	//Rounded Up
				syU = ((y-1)>>lod_diff)+1;
				sxD = x>>lod_diff;			//Rounded Down
				syD = y>>lod_diff;

				if(	touch_buffer[sxD + syD*touch_buffer_size] ||
					touch_buffer[sxU + syD*touch_buffer_size] ||
					touch_buffer[sxD + syU*touch_buffer_size] ||
					touch_buffer[sxU + syU*touch_buffer_size] )
					continue;

				copyVec3(backup_buffer->data_u8vec3[x+y*backup_buffer->size], buffer->data_u8vec3[x+y*buffer->size]);
			}
		}
		return;
	case TERRAIN_BUFFER_OBJECTS:
		for(i=0; i < eaSize(&buffer->data_objects); i++)
		{
			if(	i >= eaSize(&backup_buffer->data_objects) || 
				backup_buffer->data_objects[i]->density		)
			{				
				for(j=lod_diff; j > 0; j--)
				{
					terrainSubdivObjectDensity( buffer->data_objects[i]->density, 1<<(j-1), buffer->size, buffer->size );
				}
				if(i < eaSize(&backup_buffer->data_objects))
				{
					assert(	backup_buffer->data_objects[i]->object_type == 
							buffer->data_objects[i]->object_type			);

					//Copy in old data for data not touched
					for(x=0 ; x < buffer->size; x++)
					{					
						for(y=0 ; y < buffer->size; y++)
						{
							int sxU, syU, sxD, syD;
							sxU = ((x-1)>>lod_diff)+1;	//Rounded Up
							syU = ((y-1)>>lod_diff)+1;
							sxD = x>>lod_diff;			//Rounded Down
							syD = y>>lod_diff;

							if(	touch_buffer[sxD + syD*touch_buffer_size] ||
								touch_buffer[sxU + syD*touch_buffer_size] ||
								touch_buffer[sxD + syU*touch_buffer_size] ||
								touch_buffer[sxU + syU*touch_buffer_size] )
								continue;

							buffer->data_objects[i]->density[x+y*buffer->size] =
									backup_buffer->data_objects[i]->density[x+y*buffer->size];
						}
					}
				}
			}
		}
		return;
	case TERRAIN_BUFFER_ATTR:
		for(j=lod_diff; j > 0; j--)
		{
			terrainSubdivAttr(buffer, 1<<(j-1));
		}
		//Copy in old data for data not touched
		for(x=0 ; x < buffer->size; x++)
		{					
			for(y=0 ; y < buffer->size; y++)
			{
				int sxU, syU, sxD, syD;
				sxU = ((x-1)>>lod_diff)+1;	//Rounded Up
				syU = ((y-1)>>lod_diff)+1;
				sxD = x>>lod_diff;			//Rounded Down
				syD = y>>lod_diff;

				if(	touch_buffer[sxD + syD*touch_buffer_size] ||
					touch_buffer[sxU + syD*touch_buffer_size] ||
					touch_buffer[sxD + syU*touch_buffer_size] ||
					touch_buffer[sxU + syU*touch_buffer_size] )
					continue;

				buffer->data_byte[x+y*buffer->size] = 
					backup_buffer->data_byte[x+y*backup_buffer->size];
			}
		}
		return;
	}
}

void heightMapDoUpsamples(HeightMap *height_map, int draw_lod, bool keep_high_res)
{	
	int i;

	for(i=0; i < TERRAIN_BUFFER_NUM_TYPES; i++)
	{
		if(height_map->backup_buffer[i] != NULL)
		{
			if(height_map->touch_buffer)
				heightMapSubDivBuffer(height_map, i, height_map->backup_buffer[i], height_map->touch_buffer, draw_lod, keep_high_res);
			if (height_map->backup_buffer[i]->type == TERRAIN_BUFFER_HEIGHT || height_map->backup_buffer[i]->type == TERRAIN_BUFFER_MATERIAL)
				heightMapUpdateGeometry(height_map);
            else if (height_map->backup_buffer[i]->type == TERRAIN_BUFFER_COLOR || height_map->backup_buffer[i]->type == TERRAIN_BUFFER_ALPHA)
                heightMapUpdateTextures(height_map);
            else
                heightMapUpdateVisualization(height_map);
		}
	}

	if(height_map->touch_buffer)
	{
		free(height_map->touch_buffer);
		height_map->touch_buffer = NULL;
	}
}

static const char *s_DetailTexture;
AUTO_RUN;
void wlTerrain_InitStrings(void)
{
	s_DetailTexture = allocAddStaticString("DetailTexture");
}

const char **heightMapGetDetailTextures(HeightMap *height_map)
{
	static const char *detail_textures[NUM_MATERIALS];
	ShaderOperationSpecificValue *val = NULL;
	int i;

	for(i=0; i < NUM_MATERIALS; i++)
	{
		detail_textures[i] = "";
		if (height_map->material_ids[i] != 0xff)
		{
			char *material_name = terrainGetLayerMaterial(height_map->zone_map_layer, height_map->material_ids[i]);
			Material *material = materialFind(material_name, WL_FOR_WORLD);
			detail_textures[i] = materialGetStringValue(material, s_DetailTexture);
			if (!detail_textures[i] || detail_textures[i][0] == '\0')
			{
				if(height_map->zone_map_layer)
					ErrorFilenamef(layerGetFilename(height_map->zone_map_layer), "Terrain material '%s' is missing or invalid.", material_name);
				else
					Errorf("Terrain material '%s' is missing or invalid: Unknown Layer", material_name);
			}
		}
	}

	return detail_textures;
}

Material *terrainGetMaterial(HeightMap *height_map, F32 x, F32 z)
{
	int i;
	const char *material_name;
	TerrainMaterialWeight *mat_weights;
	TerrainBuffer *buffer;
	F32 highest_weight=0;
	U8 highest_weight_idx=0;
	U32 sx, sz;
	Vec3 world_pos;

	if( !height_map	)
		return NULL;

	addVec3(height_map->bounds.local_min, height_map->bounds.offset, world_pos);
	world_pos[0] = x - world_pos[0];
	world_pos[2] = z - world_pos[2];
	sx = (U32)((world_pos[0]*1.f/(1<<height_map->loaded_level_of_detail)) + 0.5f);
	sz = (U32)((world_pos[2]*1.f/(1<<height_map->loaded_level_of_detail)) + 0.5f);

	if(sx < 0 || sz < 0 || sx >= height_map->size || sz >= height_map->size)
		return NULL;


	buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);
	if(!buffer)
		return NULL;

	mat_weights = &buffer->data_material[sz*height_map->size + sx];

	for(i=0; i < NUM_MATERIALS; i++)
	{
		if (highest_weight < mat_weights->weights[i])
			highest_weight_idx = i;
	}


	material_name = terrainGetLayerMaterial(height_map->zone_map_layer, height_map->material_ids[highest_weight_idx]);

	if(!material_name)
		return NULL;

	return materialFind(material_name, WL_FOR_WORLD);
}

PhysicalProperties *terrainGetPhysicalProperties(HeightMap *height_map, F32 x, F32 z)
{
	Material *material = terrainGetMaterial(height_map, x, z);
	if(!material)
		return NULL;
	return GET_REF(material->world_props.physical_properties);
}

bool terrainGetInterpolatedValues(F32 x, F32 z, HeightMap *map, F32 mapx, F32 mapy, int *startX, int *startY, F32 *interpX, F32 *interpY)
{
	if (x >= 0)
		(*interpX) = fmodf(x,GRID_BLOCK_SIZE) / (1 << map->loaded_level_of_detail);
	else
		(*interpX) = (GRID_BLOCK_SIZE - fmodf(-x,GRID_BLOCK_SIZE)) / (1 << map->loaded_level_of_detail);
	if (z >= 0)	
		(*interpY) = fmodf(z,GRID_BLOCK_SIZE) / (1 << map->loaded_level_of_detail);
	else
		(*interpY) = (GRID_BLOCK_SIZE - fmodf(-z,GRID_BLOCK_SIZE)) / (1 << map->loaded_level_of_detail);

	if (fabsf((*interpX) - mapx) >= 1.0 || fabsf((*interpY) - mapy) >= 1.0)
		return false;
	// Wrapped around, wrong map

	if ((*interpX) < mapx)
	{
		(*startX) = mapx - 1;
	}
	else
	{		
		(*startX) = mapx;
	}
	if ((*interpY) < mapy)
	{
		(*startY) = mapy - 1;
	}
	else
	{			
		(*startY) = mapy;
	}

	// Normalize interpolation
	(*interpX) -= (*startX);
	(*interpY) -= (*startY);

	return true;
}

F32		heightMapGetInterpolatedHeight(HeightMap *interpMap, U32 startX, U32 startY, F32 interpX, F32 interpY)
{
	F32 heights[2][2] = { { 0, 0 }, { 0, 0 } };
	F32 temp, temp2;
	
	//assert(interpX >= 0 && interpX <= 1.0 && interpY >= 0 && interpY <= 1.0);

#if 0 // Triangles at 0,0 and 1,1
	// If we're past the tessellation point
	if (interpX + interpY >= 1)
	{
		heights[0][1] = heightMapGetHeight(interpMap,startX, startY + 1);
		heights[1][0] = heightMapGetHeight(interpMap,startX + 1, startY);
		heights[1][1] = heightMapGetHeight(interpMap,startX + 1, startY + 1);

		heights[0][0] = ((heights[0][1] + heights[1][0])/2 - heights[1][1]) * 2 + heights[1][1];
		// Extrapolate the point to make a coplanar rectangle
	}
	else
	{
		heights[0][0] = heightMapGetHeight(interpMap,startX, startY);
		heights[0][1] = heightMapGetHeight(interpMap,startX, startY + 1);
		heights[1][0] = heightMapGetHeight(interpMap,startX + 1, startY);
		
		heights[1][1] = ((heights[0][1] + heights[1][0])/2 - heights[0][0]) * 2 + heights[0][0];
		// Extrapolate the point to make a coplanar rectangle
	}
#else // Triangles at 1,0 and 0,1
	// Don't attempt to sample extra points if we're at an edge
	// If we're past the tessellation point
	if (interpX + (1 - interpY) >= 1)
	{
		heights[0][0] = heightMapGetHeight(interpMap,startX, startY);
		if (interpX > 0.0001)
		{
			heights[1][0] = heightMapGetHeight(interpMap,startX + 1, startY);
			if (interpY > 0.0001) heights[1][1] = heightMapGetHeight(interpMap,startX + 1, startY + 1);
		}

		if (interpY > 0.0001) heights[0][1] = ((heights[0][0] + heights[1][1])/2 - heights[1][0]) * 2 + heights[1][0];
		// Extrapolate the point to make a coplanar rectangle
	}
	else
	{
		heights[0][0] = heightMapGetHeight(interpMap,startX, startY);
		if (interpY > 0.0001)
		{
			heights[0][1] = heightMapGetHeight(interpMap,startX, startY + 1);
			if (interpX > 0.0001) heights[1][1] = heightMapGetHeight(interpMap,startX + 1, startY + 1);
		}

		if (interpX > 0.0001) heights[1][0] = ((heights[0][0] + heights[1][1])/2 - heights[0][1]) * 2 + heights[0][1];
		// Extrapolate the point to make a coplanar rectangle

	}
#endif
	
	// Interpolate using the new coplanar rectangle

	// Interpolate on Y, get endpoints
	temp = ((heights[0][1] - heights[0][0]) * interpY) + heights[0][0];
	temp2 = ((heights[1][1] - heights[1][0]) * interpY) + heights[1][0];
	// Interpolate on X
	return ((temp2 - temp) * interpX) + temp;
}

void heightMapGetInterpolatedNormal(HeightMap *interpMap, U32 startX, U32 startY, F32 interpX, F32 interpY, U32 lod, U8Vec3 normal)
{
	U8Vec3 norm1, norm2;
	Vec3 x1, x2;

	if(interpY > 0.0001)
	{
		heightMapGetNormal(interpMap, (startX),		(startY),	lod, norm1);
		heightMapGetNormal(interpMap, (startX),		(startY+1), lod, norm2);
		subVec3(norm2, norm1, x1);
		scaleVec3(x1, interpY, x1);
		addVec3(x1, norm1, x1);

        if (interpX > 0.0001)
        {
            heightMapGetNormal(interpMap, (startX+1),	(startY),	lod, norm1);
            heightMapGetNormal(interpMap, (startX+1),	(startY+1), lod, norm2);			
            subVec3(norm2, norm1, x2);
            scaleVec3(x2, interpY, x2);
            addVec3(x2, norm1, x2);
        }
	}
	else
	{
		heightMapGetNormal(interpMap, (startX),		(startY),	lod, norm1);
		copyVec3(norm1, x1);

        if (interpX > 0.0001)
        {
            heightMapGetNormal(interpMap, (startX+1),	(startY),	lod, norm1);	
            copyVec3(norm1, x2);
        }
	}

	if(interpX > 0.0001)
	{
		subVec3(x2, x1, x2);
		scaleVec3(x2, interpX, x2);
		addVec3(x2, x1, x1);		
	}
	
	copyVec3(x1, normal);
}

F32 heightMapGetInterpolatedShadow(HeightMap *interpMap, U32 startX, U32 startY, F32 interpX, F32 interpY)
{
    F32 shadow1, shadow2;
	F32 x1, x2;

	if(interpY > 0.0001)
	{
		shadow1 = heightMapGetShadow(interpMap, (startX),		(startY));
		shadow2 = heightMapGetShadow(interpMap, (startX),		(startY+1));
		x1 = lerp(shadow1, shadow2, interpY);

        if (interpX > 0.0001)
        {
            shadow1 = heightMapGetShadow(interpMap, (startX+1),	(startY));
            shadow2 = heightMapGetShadow(interpMap, (startX+1),	(startY+1));			
            x2 = lerp(shadow1, shadow2, interpY);
        }
	}
	else
	{
		shadow1 = heightMapGetShadow(interpMap, (startX),		(startY));
        x1 = shadow1;

        if (interpX > 0.0001)
        {
            shadow1 = heightMapGetShadow(interpMap, (startX+1),	(startY));
            x2 = shadow1;
        }
	}

	if(interpX > 0.0001)
	{
        return lerp(x1, x2, interpX);
	}
	
	return x1;
}

void heightMapGetInterpolatedSoilDepth(HeightMap *interpMap, U32 startX, U32 startY, F32 interpX, F32 interpY, F32 *depth)
{
	F32 depth1, depth2;
	F32 x1, x2;

	if(interpY > 0.0001)
	{
		depth1 = heightMapGetSoilDepth(interpMap, (startX), (startY));
		depth2 = heightMapGetSoilDepth(interpMap, (startX), (startY+1));
		x1 = (depth2-depth1)*interpY + depth1;

		if (interpX > 0.0001)
		{
			depth1 = heightMapGetSoilDepth(interpMap, (startX+1), (startY));
			depth2 = heightMapGetSoilDepth(interpMap, (startX+1), (startY+1));
			x2 = (depth2-depth1)*interpY + depth1;
		}
	}
	else
	{
		x1 = heightMapGetSoilDepth(interpMap, (startX), (startY));

		if (interpX > 0.0001)
			x2 = heightMapGetSoilDepth(interpMap, (startX+1), (startY));
	}

	if(interpX > 0.0001)
		x1 = (x2-x1)*interpX + x1;
	
	(*depth) = x1;
}

bool heightMapEdgeIsLocked(HeightMap *map, int x, int y)
{
	if (map->edge_locked[2] && y == map->size-1) return true;
	if (map->edge_locked[3] && x == map->size-1) return true;
	if (map->edge_locked[0] && y == 0) return true;
	if (map->edge_locked[1] && x == 0) return true;
	return false;
}


bool terrainCheckAngleAndHeight(SA_PARAM_NN_VALID ZoneMapLayer *layer, F32 x, F32 z, U8 min_angle, U8 max_angle, F32 min_height, F32 max_height, HeightMap **cache)
{
	/*int num_maps = 0;
	HeightMap* map = 0;
	int mapx;
	int mapy;

	terrainGetTouchingHeightMaps( layer, x, z, 1, &num_maps, &map, &mapx, &mapy, false, cache );
	if (num_maps > 0)
	{
		U8 normal[3];
		U8 UpVec;
		F32 height;

		if(min_angle != max_angle)
		{
			heightMapGetNormal(map, mapx, mapy, map->loaded_level_of_detail, normal);
			UpVec = 255 - normal[1];
			if(	min_angle < max_angle &&
				(UpVec < min_angle || UpVec > max_angle))
				return false;
			else if(min_angle > max_angle &&
					(UpVec > max_angle && UpVec < min_angle))
				return false;
		}

		if(min_height != max_height)
		{
			height = heightMapGetHeight(map, mapx, mapy);
			if(	min_height < max_height &&
				(height < min_height || height > max_height))
				return false;
			else if(min_height > max_height &&
					(height > max_height && height < min_height))
				return false;
		}
	}
	else
	{
		return false;
	}
	return true;*/
    return false;
}

void heightMapResample(HeightMap *height_map, U32 lod)
{
	S32 new_lod;
	if (height_map->loaded_level_of_detail == lod) return;

	heightMapDeleteBuffer(height_map, TERRAIN_BUFFER_SELECTION, -1);
	if (height_map->loaded_level_of_detail > lod)
	{
		// increase resolution of heightmap
		int stride = 1 << (height_map->loaded_level_of_detail-lod);
		int size = GRID_LOD(lod);
		TerrainBuffer *old_buf = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
		assert(old_buf);
		{
			int x, z;
			int step = stride;
			TerrainBuffer *new_buf = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_HEIGHT, lod);
			for (z = 0; z < size; z += stride)
				for (x = 0; x < size; x += stride)
					new_buf->data_f32[x+z*size] = old_buf->data_f32[(x/stride)+(z/stride)*old_buf->size];

			for (new_lod = height_map->loaded_level_of_detail-1; new_lod >= (S32)lod; new_lod--)
			{
				step /= 2;
				terrainSubdivF32( new_buf, step );
			}			
		}
        DestroyTerrainBuffer(old_buf);
		// upsample normals
		old_buf = heightMapGetBuffer(height_map, TERRAIN_BUFFER_NORMAL, height_map->loaded_level_of_detail);
		if (old_buf)
		{
			int x, z;
			int step = stride;
			TerrainBuffer *new_buf = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_NORMAL, lod);
			for (z = 0; z < size; z += stride)
				for (x = 0; x < size; x += stride)
					copyVec3(old_buf->data_normal[(x/stride)+(z/stride)*old_buf->size], new_buf->data_normal[x+z*size]);

			for (new_lod = height_map->loaded_level_of_detail-1; new_lod >= (S32)lod; new_lod--)
			{
				step /= 2;
				terrainSubdivU8Vec3( new_buf, step );
			}			
		}
		// increase resolution of alpha map
		old_buf = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_ALPHA, height_map->loaded_level_of_detail);
		if (old_buf)
		{
			int x, z;
			int step = stride;
			TerrainBuffer *new_buf = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_ALPHA, lod);
			for (z = 0; z < size; z += stride)
				for (x = 0; x < size; x += stride)
					new_buf->data_byte[x+z*size] = old_buf->data_byte[(x/stride)+(z/stride)*old_buf->size];

			for (new_lod = height_map->loaded_level_of_detail-1; new_lod >= (S32)lod; new_lod--)
			{
				step /= 2;
				terrainSubdivAlpha( new_buf, step );
			}			
            DestroyTerrainBuffer(old_buf);
		}
		// create color maps
		for (new_lod = GET_COLOR_LOD(height_map->loaded_level_of_detail)-1; new_lod >= GET_COLOR_LOD((S32)lod); new_lod--)
		{
			int x, z;
			int cur_size = GRID_LOD(new_lod);
			int old_size = GRID_LOD(new_lod+1);
			TerrainBuffer *color_buffer;
			TerrainBuffer *old_color_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_COLOR, new_lod+1);

			heightMapDeleteBuffer(height_map, TERRAIN_BUFFER_COLOR, new_lod);
			color_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_COLOR, new_lod);
			
			//If old low res does not exist then just clear this one and move on.
			if(!old_color_buffer)
			{
				ClearTerrainBuffer(height_map->zone_map_layer, color_buffer);
				continue;
			}

			//Copy From Last
			for (z = 0; z < old_size; z++)
			{
				for (x = 0; x < old_size; x++)
				{
					copyVec3(old_color_buffer->data_u8vec3[z*old_color_buffer->size + x], color_buffer->data_u8vec3[(z<<1)*color_buffer->size + (x<<1)]);
				}
			}
			//Upsample
			terrainSubdivU8Vec3(color_buffer,1); 
		}
		// upsample material map
		{
			int x, z;
			IVec2 unused = {0};
			int step = stride;

			TerrainBuffer *old_material_buffer = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);
			TerrainBuffer *material_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_MATERIAL, lod);

			if (old_material_buffer && material_buffer)
			{
				for (z = 0; z < size; z += stride)
					for (x = 0; x < size; x += stride)
						material_buffer->data_material[x+z*size] = old_material_buffer->data_material[(x/stride) + (z/stride)*old_material_buffer->size];

				for (new_lod = height_map->loaded_level_of_detail-1; new_lod >= (S32)lod; new_lod--)
				{
					step /= 2;
					terrainSubdivMaterialWeights(material_buffer, step);
				}
			}
            if (old_material_buffer)
		        DestroyTerrainBuffer(old_material_buffer);
		}
		// upsample soil map
		{
			int x, z;
			IVec2 unused = {0};
			int step = stride;

			TerrainBuffer *old_soil_depth_buffer = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, height_map->loaded_level_of_detail);
			TerrainBuffer *soil_depth_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, lod);

			if (old_soil_depth_buffer && soil_depth_buffer)
			{
				for (z = 0; z < size; z += stride)
					for (x = 0; x < size; x += stride)
						soil_depth_buffer->data_f32[x+z*size] = old_soil_depth_buffer->data_f32[(x/stride) + (z/stride)*old_soil_depth_buffer->size];

				for (new_lod = height_map->loaded_level_of_detail-1; new_lod >= (S32)lod; new_lod--)
				{
					step /= 2;
					terrainSubdivF32(soil_depth_buffer, step);
				}
			}
            if (old_soil_depth_buffer)
		        DestroyTerrainBuffer(old_soil_depth_buffer);
		}

		// upsample attr map
		{
			int x, z;
			IVec2 unused = {0};
			int step = stride;

			TerrainBuffer *old_attr_buffer = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_ATTR, height_map->loaded_level_of_detail);
			TerrainBuffer *attr_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_ATTR, lod);

			if (old_attr_buffer && attr_buffer)
			{
				for (z = 0; z < size; z += stride)
					for (x = 0; x < size; x += stride)
						attr_buffer->data_byte[x+z*size] = old_attr_buffer->data_byte[(x/stride) + (z/stride)*old_attr_buffer->size];

				for (new_lod = height_map->loaded_level_of_detail-1; new_lod >= (S32)lod; new_lod--)
				{
					step /= 2;
					terrainSubdivAttr(attr_buffer, step);
				}
			}
            if (old_attr_buffer)
		        DestroyTerrainBuffer(old_attr_buffer);
		}

		// upsample objects map
		{
			int x, z, i;
			int step = stride;

			TerrainBuffer *old_objects_buffer = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
			TerrainBuffer *objects_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_OBJECTS, lod);

			if (old_objects_buffer && objects_buffer)
			{
				for(i=0; i < eaSize(&old_objects_buffer->data_objects); i++)
				{
					TerrainObjectBuffer *new_sub_buffer = calloc(1, sizeof(TerrainObjectBuffer));
					new_sub_buffer->object_type = old_objects_buffer->data_objects[i]->object_type;
					new_sub_buffer->density = calloc(size*size, sizeof(TerrainObjectDensity));
					eaPush(&objects_buffer->data_objects, new_sub_buffer);
						
					for (z = 0; z < size; z += stride)
					{
						for (x = 0; x < size; x += stride)
						{
							new_sub_buffer->density[x+z*size] = old_objects_buffer->data_objects[i]->density[(x/stride) + (z/stride)*old_objects_buffer->size];
						}
					}
				}

				for (new_lod = height_map->loaded_level_of_detail-1; new_lod >= (S32)lod; new_lod--)
				{
					step /= 2;
					for(i=0; i < eaSize(&objects_buffer->data_objects); i++)
					{
						terrainSubdivObjectDensity(objects_buffer->data_objects[i]->density, step,objects_buffer->size, objects_buffer->size);
					}
				}
			}
            if (old_objects_buffer)
	        	DestroyTerrainBuffer(old_objects_buffer);
		}
	}
	else // (lod > height_map->loaded_level_of_detail)
	{
		// decrease resolution of heightmap
		int size = GRID_LOD(lod);
		int stride = 1 << (lod - height_map->loaded_level_of_detail);
		TerrainBuffer *old_buf = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);
		if (old_buf)
		{
			int x, z;
			TerrainBuffer *new_buf = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_HEIGHT, lod);
			for (z = 0; z < size; z++)
				for (x = 0; x < size; x++)
					new_buf->data_f32[x+z*size] = old_buf->data_f32[(x*stride)+(z*stride)*old_buf->size];
            DestroyTerrainBuffer(old_buf);
		}

		// decrease resolution of alpha map
		old_buf = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_ALPHA, height_map->loaded_level_of_detail);
		if (old_buf)
		{
			int x, z;
			TerrainBuffer *new_buf = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_ALPHA, lod);
			for (z = 0; z < size; z++)
				for (x = 0; x < size; x++)
					new_buf->data_byte[x+z*size] = old_buf->data_byte[(x*stride)+(z*stride)*old_buf->size];
            DestroyTerrainBuffer(old_buf);
		}

		// delete lower mip color/normal buffers
		for (new_lod = GET_COLOR_LOD(height_map->loaded_level_of_detail); new_lod < (S32)GET_COLOR_LOD(lod); new_lod++)
		{
			heightMapDeleteBuffer(height_map, TERRAIN_BUFFER_COLOR, new_lod);
		}
		for (new_lod = height_map->loaded_level_of_detail; new_lod < (S32)lod; new_lod++)
		{
			heightMapDeleteBuffer(height_map, TERRAIN_BUFFER_NORMAL, new_lod);
		}

		// downsample material map
		{
			int x, z;

			TerrainBuffer *old_material_buffer = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);
			TerrainBuffer *material_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_MATERIAL, lod);

			if (old_material_buffer && material_buffer)
			{
				for (z = 0; z < size; z++)
					for (x = 0; x < size; x++)
						material_buffer->data_material[x+z*size] = old_material_buffer->data_material[(x*stride) + (z*stride)*old_material_buffer->size];
			}
            if (old_material_buffer)
	            DestroyTerrainBuffer(old_material_buffer);
		}
		// downsample soil depth map
		{
			int x, z;

			TerrainBuffer *old_soil_depth_buffer = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, height_map->loaded_level_of_detail);
			TerrainBuffer *soil_depth_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_SOIL_DEPTH, lod);

			if (old_soil_depth_buffer && soil_depth_buffer)
			{
				for (z = 0; z < size; z++)
					for (x = 0; x < size; x++)
						soil_depth_buffer->data_f32[x+z*size] = old_soil_depth_buffer->data_f32[(x*stride) + (z*stride)*old_soil_depth_buffer->size];
			}
            if (old_soil_depth_buffer)
	            DestroyTerrainBuffer(old_soil_depth_buffer);
		}
		// downsample objects map
		{
			int x, z, i;
			int step = stride;

			TerrainBuffer *old_objects_buffer = heightMapDetachBuffer(height_map, TERRAIN_BUFFER_OBJECTS, height_map->loaded_level_of_detail);
			TerrainBuffer *objects_buffer = heightMapCreateBuffer(height_map, TERRAIN_BUFFER_OBJECTS, lod);

			if (old_objects_buffer && objects_buffer)
			{
				for(i=0; i < eaSize(&old_objects_buffer->data_objects); i++)
				{
					TerrainObjectBuffer *new_sub_buffer = calloc(1, sizeof(TerrainObjectBuffer));
					new_sub_buffer->object_type = old_objects_buffer->data_objects[i]->object_type;
					new_sub_buffer->density = calloc(size*size, sizeof(TerrainObjectDensity));
					eaPush(&objects_buffer->data_objects, new_sub_buffer);
						
					for (z = 0; z < size; z++)
					{
						for (x = 0; x < size; x++)
						{
							new_sub_buffer->density[x+z*size] = old_objects_buffer->data_objects[i]->density[(x*stride) + (z*stride)*old_objects_buffer->size];
						}
					}
				}
			}
            if (old_objects_buffer)
	            DestroyTerrainBuffer(old_objects_buffer);
            heightMapDeleteBuffer(height_map, TERRAIN_BUFFER_ATTR, -1);
		}
	}

	height_map->size = GRID_LOD(lod);
	height_map->loaded_level_of_detail = lod;
	height_map->level_of_detail = lod;
	heightMapModify(height_map);
    heightMapUpdate(height_map);
}

//////////////////////////////////////////////////////////////////////////

static PSDKCookedMesh* heightMapCookMesh(HeightMap *height_map)
{
	PSDKCookedMesh** mesh;

	if (!height_map)
		return NULL;

	terrainLock();

	mesh = &height_map->psdkCookedMeshEditor;

	if (height_map->editor_cooked_mesh_mod_time != height_map->mod_time ||
		height_map->editor_cooked_mesh_lod != height_map->loaded_level_of_detail)
	{
#if !PSDK_DISABLED
		psdkCookedMeshDestroy(mesh);
#endif
	}

#if !PSDK_DISABLED
	if (!*mesh)
	{
		PERFINFO_AUTO_START_FUNC();
		{
			U32 i, j;
			U32 map_size = heightMapGetSize(height_map);
			F32* temp_map = calloc( map_size*map_size, sizeof(F32) );
			bool* hole_map = calloc( map_size*map_size, sizeof(bool) );
			int lod = heightMapGetLoadedLOD(height_map);
			PSDKHeightFieldDesc heightFieldDesc = {0};

			addVec3(height_map->bounds.local_min,
					height_map->bounds.offset,
					heightFieldDesc.debug.worldMin);

			addVec3(height_map->bounds.local_max,
					height_map->bounds.offset,
					heightFieldDesc.debug.worldMax);

			setVec2(heightFieldDesc.gridSize, map_size, map_size);
			setVec2(heightFieldDesc.worldSize, GRID_BLOCK_SIZE, GRID_BLOCK_SIZE);

			heightFieldDesc.height = temp_map;
			heightFieldDesc.holes = hole_map;

			for (i = 0; i < map_size; i++)
			{
				for (j = 0; j < map_size; j++)
				{
					temp_map[j + i*map_size] = heightMapGetHeight(height_map, j, i);
					hole_map[j + i*map_size] = false;
				}
			}

			psdkCookedHeightFieldCreate(mesh, &heightFieldDesc);
			if (!*mesh) printf("Failed to create terrain mesh (%d,%d), lod %d!\n", height_map->map_local_pos[0],
				height_map->map_local_pos[1], height_map->loaded_level_of_detail);
			/*else printf("Created terrain mesh (%d,%d), lod %d.\n", height_map->map_local_pos[0],
				height_map->map_local_pos[1], height_map->loaded_level_of_detail);*/

			SAFE_FREE(temp_map);
			SAFE_FREE(hole_map);

			height_map->editor_cooked_mesh_mod_time = height_map->mod_time;
			height_map->editor_cooked_mesh_lod = height_map->loaded_level_of_detail;
		}
		PERFINFO_AUTO_STOP_FUNC();
	}
#endif

	terrainUnlock();

	return *mesh;
}

void heightMapCollObjectMsgHandler(const WorldCollObjectMsg *msg)
{
	HeightMap *height_map = msg->userPointer;

	switch (msg->msgType)
	{
		xcase WCO_MSG_GET_DEBUG_STRING:
		{
			Vec3 world_min, world_max;
			addVec3(height_map->bounds.local_min, height_map->bounds.offset, world_min);
			addVec3(height_map->bounds.local_max, height_map->bounds.offset, world_max);
			snprintf_s(	msg->in.getDebugString.buffer,
						msg->in.getDebugString.bufferLen,
						"HeightMap: (%1.2f, %1.2f, %1.2f) - (%1.2f, %1.2f, %1.2f)",
						vecParamsXYZ(world_min),
						vecParamsXYZ(world_max));
		}
		
		xcase WCO_MSG_DESTROYED:
		{
			assert(height_map->wcoEditor == msg->wco);
			height_map->wcoEditor = NULL;
		}

		xcase WCO_MSG_GET_SHAPE:
		{
			WorldCollObjectMsgGetShapeOut*	getShape = msg->out.getShape;
			WorldCollObjectMsgGetShapeOutInst* shapeInst;

			copyMat3(unitmat, getShape->mat);
			copyVec3(height_map->bounds.offset, getShape->mat[3]);

			wcoAddShapeInstance(getShape, &shapeInst);
			shapeInst->mesh = heightMapCookMesh(height_map);
			shapeInst->filter.shapeGroup = WC_SHAPEGROUP_HEIGHTMAP;
			shapeInst->filter.filterBits = WC_FILTER_BITS_HEIGHTMAP;

			shapeInst->material = wcMaterial;
		}
	}
}

static F32 heightMapCalcTileBounds(HeightMap *height_map, TerrainBlockRange *range, Vec3 tile_min, Vec3 tile_max, Vec3 tile_mid)
{
	U32 i;
	TerrainBuffer *height_buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_HEIGHT, height_map->loaded_level_of_detail);

	setVec3(tile_min, 0, 8e16, 0);
	setVec3(tile_max, GRID_BLOCK_SIZE, -8e16, GRID_BLOCK_SIZE);

	if (range && range->map_ranges_array)
	{
		int ranges_idx = ((range->range.max_block[0]+1-range->range.min_block[0]) * (height_map->map_local_pos[1]-range->range.min_block[2]) + (height_map->map_local_pos[0]-range->range.min_block[0]));
		tile_min[1] = range->map_ranges_array[ranges_idx*2];
		tile_max[1] = range->map_ranges_array[ranges_idx*2+1];
	}
	else if (height_buffer)
	{
		for (i = 0; i < height_map->size * height_map->size; ++i)
		{
			MIN1(tile_min[1], height_buffer->data_f32[i]);
			MAX1(tile_max[1], height_buffer->data_f32[i]);
		}
	}
    if (tile_min[1] > tile_max[1])
    {
        tile_min[1] = 0.f;
        tile_max[1] = 1.f;
    }

	addVec3(tile_min, tile_max, tile_mid);
	scaleVec3(tile_mid, 0.5f, tile_mid);
	return MIN(2500.f, 0.5f * distance3(tile_min, tile_max));
}

void heightMapCreateCollision(HeightMapAtlasData *atlas_data, ZoneMapLayer *layer)
{
	GroupInfo info = {0};

	if (!atlas_data || atlas_data->collision_entry || !atlas_data->collision_model)
		return;

	copyMat3(unitmat, info.world_matrix);
	copyVec3(atlas_data->offset, info.world_matrix[3]);
	addVec3(atlas_data->collision_model->min, atlas_data->offset, info.world_min);
	addVec3(atlas_data->collision_model->max, atlas_data->offset, info.world_max);
	centerVec3(info.world_min, info.world_max, info.world_mid);
	info.radius = atlas_data->collision_model->radius;
	info.region = atlas_data->region;
	info.uniform_scale = 1;
	info.current_scale = 1;

	atlas_data->collision_entry = createWorldCollisionEntry(NULL, NULL, atlas_data->collision_model, NULL, NULL, NULL, &info, true);
	atlas_data->collision_entry->base_entry_data.group_id = getLayerIDBits(layer);
}

void heightMapDestroyCollision(HeightMapAtlasData *atlas_data)
{
	if (atlas_data && atlas_data->collision_entry)
	{
		worldCellEntryFree(&atlas_data->collision_entry->base_entry);
		atlas_data->collision_entry = NULL;
	}
}

void heightMapTrackerOpenEntries(int iPartitionIdx, HeightMapTracker *tracker)
{
	if (!tracker)
		return;

	if (!wcMaterial)
	{
#if !PSDK_DISABLED
	PSDKMaterialDesc md = {0};

		md.dynamicFriction = 100.f;
		md.staticFriction = 100.f;
		md.restitution = 0.f;

		wcMaterialGetByIndex(&wcMaterial, 128, &md);
#endif
	}

	heightMapCreateCollision(tracker->height_map->data, tracker->height_map->zone_map_layer);
	worldCellEntryOpen(iPartitionIdx, &tracker->height_map->data->collision_entry->base_entry, tracker->height_map->data->region);


	if (wlIsClient())
	{
		if (!tracker->height_map->wcoEditor)
		{
			Vec3 min, max;
			addVec3(tracker->height_map->bounds.local_min, tracker->height_map->bounds.offset, min);
			addVec3(tracker->height_map->bounds.local_max, tracker->height_map->bounds.offset, max);
			wcoCreate(	&tracker->height_map->wcoEditor,
						worldGetActiveColl(iPartitionIdx),
						heightMapCollObjectMsgHandler,
						tracker->height_map,
						min,
						max,
						0,
						0);
		}
    }
}

static void heightMapTrackerFreeEntryData(HeightMapTracker *tracker)
{
	heightMapDestroyCollision(tracker->height_map->data);
	if (!wlIsServer())
	{
		wcoDestroy(&tracker->height_map->wcoEditor);
	}
}

void heightMapTrackerCloseEntries(HeightMapTracker *tracker)
{
	heightMapTrackerFreeEntryData(tracker);
}

void heightMapTrackerUpdate(HeightMapTracker *tracker, bool force, bool update_collision, bool use_bin_bounds)
{
	HeightMapAtlasData *atlas_data = SAFE_MEMBER2(tracker, height_map, data);
	TerrainBlockRange *range = NULL;
	Vec3 tile_mid;

	if (!tracker || !tracker->height_map)
		return;

	if (!force && tracker->height_map->mod_time == tracker->height_map_mod_time)
		return;

	// calc world bounds
	copyVec2(tracker->height_map->map_local_pos, tracker->world_pos);

	if (use_bin_bounds)
		range = layerGetTerrainBlock(tracker->height_map->zone_map_layer, 
			layerGetTerrainBlockForLocalPos(tracker->height_map->zone_map_layer, tracker->height_map->map_local_pos));

	tracker->height_map->bounds.radius = heightMapCalcTileBounds(tracker->height_map, range, tracker->height_map->bounds.local_min, tracker->height_map->bounds.local_max, tile_mid);
    setVec3(tracker->height_map->bounds.offset, tracker->world_pos[0] * GRID_BLOCK_SIZE, 0, tracker->world_pos[1] * GRID_BLOCK_SIZE);
    addVec3(tracker->height_map->bounds.offset, tile_mid, tracker->height_map->bounds.world_mid);
	
#if !PSDK_DISABLED
	if (update_collision)
	{
		if (atlas_data && atlas_data->collision_entry && atlas_data->collision_model != atlas_data->collision_entry->model)
			heightMapDestroyCollision(atlas_data);

		if (force ||
			tracker->height_map->editor_cooked_mesh_mod_time != tracker->height_map->mod_time ||
			tracker->height_map->editor_cooked_mesh_lod != tracker->height_map->loaded_level_of_detail)
		{
			terrainLock();
			psdkCookedMeshDestroy(&tracker->height_map->psdkCookedMeshEditor);
			terrainUnlock();
		}
	}
#endif

	tracker->height_map_mod_time = tracker->height_map->mod_time;
}

HeightMapTracker *heightMapTrackerOpen(HeightMap *height_map)
{
	HeightMapTracker *tracker = calloc(1, sizeof(*tracker));
	tracker->height_map = height_map;
	heightMapTrackerUpdate(tracker, true, true, true);
	return tracker;
}

void heightMapTrackerClose(HeightMapTracker *tracker)
{
	if (!tracker)
		return;

	heightMapTrackerCloseEntries(tracker);

#if !PSDK_DISABLED
	terrainLock();
	psdkCookedMeshDestroy(&tracker->height_map->psdkCookedMeshEditor);
	terrainUnlock();
#endif

	if (tracker->atlas)
	{
		tracker->atlas->data = NULL; // just a pointer to tracker->height_map->data, don't free it
		tracker->atlas->load_state = WCS_CLOSED;
		atlasFree(tracker->atlas);
	}

	free(tracker);
}

/*TerrainBuffer **heightMapGetBuffers(HeightMap *height_map)
{
	return height_map->buffers;
    }*/

void heightMapSetVisibleEdges(HeightMap *height_map, int color1, int color2, int color3, int color4)
{
	height_map->edge_color[0] = color1;
	height_map->edge_color[1] = color2;
	height_map->edge_color[2] = color3;
	height_map->edge_color[3] = color4;
}

void heightMapSetLockedEdges(HeightMap *height_map, bool locked1, bool locked2, bool locked3, bool locked4)
{
	height_map->edge_locked[0] = locked1;
	height_map->edge_locked[1] = locked2;
	height_map->edge_locked[2] = locked3;
	height_map->edge_locked[3] = locked4;
}

char *terrainGetLayerMaterial(ZoneMapLayer *layer, int index)
{
	if (index >= 0 && index < eaSize(&layer->terrain.material_table))
	{
		return layer->terrain.material_table[index];
	}
	return NULL;
}

int terrainBlockGetIndex(TerrainBlockRange *range)
{
	return range->block_idx;
}

const char *terrainBlockGetName(TerrainBlockRange *range)
{
    return range->range_name;
}

void terrainBlockGetExtents(TerrainBlockRange *range, IVec2 min, IVec2 max)
{
	setVec2(min, range->range.min_block[0], range->range.min_block[2]);
	setVec2(max, range->range.max_block[0], range->range.max_block[2]);
}

bool heightMapValidateMaterials(HeightMap *height_map, int *lookup, char **material_table)
{
    int i, x, z;
    bool ret = true;
    TerrainBuffer *buffer = heightMapGetBuffer(height_map, TERRAIN_BUFFER_MATERIAL, height_map->loaded_level_of_detail);
    if (!buffer)
        return true;
    for (i = 0; i < NUM_MATERIALS; i++)
    {
		if (height_map->material_ids[i] == 0xFF)
        {
            for (z = 0; z < buffer->size; z++)
            	for (x = 0; x < buffer->size; x++)
                    if (buffer->data_material[z*buffer->size + x].weights[i] != 0)
                    {
                        buffer->data_material[z*buffer->size + x].weights[i] = 0;
                        ret = false;
                    }
        }
        else
        {
            if (lookup)
            {
                if (height_map->material_ids[i] >= eaiSize(&lookup) ||
                	lookup[height_map->material_ids[i]] >= eaSize(&material_table))
                {
                    for (z = 0; z < buffer->size; z++)
                        for (x = 0; x < buffer->size; x++)
                            buffer->data_material[z*buffer->size + x].weights[i] = 0;
                    ret = false;
                }
            }
            else
            {
                if (height_map->material_ids[i] >= eaSize(&material_table))
                {
                    for (z = 0; z < buffer->size; z++)
                        for (x = 0; x < buffer->size; x++)
                            buffer->data_material[z*buffer->size + x].weights[i] = 0;
                    ret = false;
                }
            }
        }
    }
    return ret;
}

#undef heightMapGetSizeForLOD
#undef heightMapGetSizeLOD
#undef heightMapGetLevelOfDetail

U32 heightMapGetSizeForLOD(U32 lod)
{
	return GRID_LOD(lod);
}

U32 heightMapGetSizeLOD(HeightMap *height_map) // Size of this square heightmap, taking into account level of detail
{
	return heightMapGetSizeForLOD(height_map->level_of_detail);
}

U32 heightMapGetLevelOfDetail(HeightMap *height_map)
{
	return height_map->level_of_detail;
}

