#pragma once
GCC_SYSTEM

#include "stdtypes.h"
#include "WorldGrid.h"

typedef struct SparseGrid SparseGrid;
typedef struct Octree Octree;
typedef struct GfxLight GfxLight;
typedef struct WorldModelInstanceEntry WorldModelInstanceEntry;
typedef struct WorldRegion WorldRegion;
typedef struct RdrLight RdrLight;
typedef struct RdrAmbientLight RdrAmbientLight;
typedef struct WorldCell WorldCell;

typedef struct WorldRegionGraphicsData
{
	WorldRegion *region;
	Octree *static_light_octree;
	GfxLight **dir_lights;
} WorldRegionGraphicsData;

typedef struct WorldGraphicsData
{
	GfxLight *sun_light;
	GfxLight *sun_light_2;
	GfxLight *cam_light;
	RdrAmbientLight *outdoor_ambient_light;

	Vec3 character_ambient_light_offset_hsv;
	Vec3 character_sky_light_offset_hsv;
	Vec3 character_ground_light_offset_hsv;
	Vec3 character_side_light_offset_hsv;
	Vec3 character_diffuse_light_offset_hsv;
	Vec3 character_secondary_diffuse_light_offset_hsv;
	Vec3 character_shadow_color_light_offset_hsv;
	Vec3 character_specular_light_offset_hsv;

	SparseGrid *dynamic_lights;

	RdrLight **override_lights;
	RdrAmbientLight **override_ambient_lights;
	int used_override_ambient_lights;

	RdrAmbientLight *main_outdoor_ambient_light;
} WorldGraphicsData;

AUTO_STRUCT;
typedef struct WorldCellGraphicsData
{
	BasicTexture			**cluster_tex_swaps;
	GfxStaticObjLightCache	*light_cache;				NO_AST
	WorldCellEntrySharedBounds	shared_bounds;			NO_AST
	WorldModelEntry		proxy_entry;				NO_AST
} WorldCellGraphicsData;

extern ParseTable parse_WorldCellGraphicsData[];
#define TYPE_parse_WorldCellGraphicsData WorldCellGraphicsData

void gfxDrawWorldCombined(void);

WorldGraphicsData *gfxAllocWorldGraphicsData(void);
void gfxFreeWorldGraphicsData(WorldGraphicsData *data);

WorldRegionGraphicsData *gfxAllocWorldRegionGraphicsData(WorldRegion *region, const Vec3 min, const Vec3 max);
void gfxFreeWorldRegionGraphicsData(WorldRegion *region, bool remove_sky_group);

void gfxTickSkyData(void);

void gfxSetAmbientLight( RdrAmbientLight* light );

bool gfxCheckClusterLoaded(WorldCell *cell, bool startLoad);
void gfxWorldCellGraphicsFreeClusterTexSwaps(WorldCellGraphicsData *pWCGD);
int getMaterialDrawIndex(CONST_EARRAY_OF(MaterialDraw) material_draws);

