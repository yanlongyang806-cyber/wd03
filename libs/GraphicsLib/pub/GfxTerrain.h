#ifndef GFXTERRAIN_H
#define GFXTERRAIN_H
GCC_SYSTEM

typedef struct HeightMap HeightMap;
typedef struct HeightMapAtlas HeightMapAtlas;
typedef struct HeightMapTracker HeightMapTracker;
typedef struct Model Model;
typedef struct TerrainObjectEntry TerrainObjectEntry;
typedef struct BlockRange BlockRange;
typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct TerrainEditorSourceLayer TerrainEditorSourceLayer;
typedef struct ZoneMap ZoneMap;
typedef struct WorldRegion WorldRegion;
typedef struct BinFileList BinFileList;
typedef struct GfxLightCacheBase GfxLightCacheBase;
typedef struct WorldCollisionEntry WorldCollisionEntry;
typedef struct WorldClusterState WorldClusterState;
typedef struct HogFile HogFile;

void gfxDrawTerrainCellsCombined(void);
bool gfxMoveRemeshImagesToHogg(HogFile *hog_file, const char *image_name, const char* image_filename);
void gfxComputeTerrainLightingForBinning(ZoneMap *zmap, const char ***file_list, BinFileList *file_list2);
Model *computeAtlasVertexLightsSingleModel(HeightMapAtlas *atlas, WorldRegion *region);
GfxLightCacheBase * gfxFindTerrainLightCache(Model * collision_model, WorldCollisionEntry * collision_entry);

typedef void (*heightMapDrawFiltersVis)(HeightMap *height_map, U8 *data, int grid_size, int lod);
typedef struct GfxTerrainViewMode
{
	U8						view_mode;				//Editor Only View Modes 
	F32						view_mode_interp;		//Interpolation between full color and multiply.

	U8						walk_angle;				//Extreme Angle Data
	U8						object_type;			//Object Density
	U8						material_type;			//Material Weight
	heightMapDrawFiltersVis	filter_cb;				//Callback for the Filter View Mode
} GfxTerrainViewMode;

typedef struct GfxTerrainDebugState
{
    GfxTerrainViewMode *	view_params;
    bool					show_terrain_occlusion;		// Show occlusion mesh
    bool					show_terrain_bounds;		// Show terrain tile bounds
    bool				   	terrain_occlusion_disable;	// Toggle occlusion usage

	TerrainEditorSource *	source_data;
} GfxTerrainDebugState;

extern GfxTerrainDebugState terrain_state;
// currently only for mapsnaps
extern char const * g_pchColorTexOverride;

#endif
