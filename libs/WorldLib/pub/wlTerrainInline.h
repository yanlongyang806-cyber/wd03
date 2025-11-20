#pragma once
GCC_SYSTEM

#include "WorldGrid.h"
#include "..\wlTerrainPrivate.h"

// YVS: not the same as in wlTerrain.c

__forceinline static U32 _heightMapGetSizeForLOD(U32 lod)
{
	return GRID_LOD(lod);
}

__forceinline static U32 _heightMapGetSizeLOD(HeightMap *height_map) // Size of this square heightmap, taking into account level of detail
{
	return heightMapGetSizeForLOD(height_map->level_of_detail);
}

__forceinline static U32 _heightMapGetLevelOfDetail(HeightMap *height_map)
{
	return MAX(height_map->level_of_detail,height_map->loaded_level_of_detail);
}

#define heightMapGetSizeForLOD _heightMapGetSizeForLOD
#define heightMapGetSizeLOD _heightMapGetSizeLOD
#define heightMapGetLevelOfDetail _heightMapGetLevelOfDetail
