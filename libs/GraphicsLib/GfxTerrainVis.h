#pragma once
GCC_SYSTEM

typedef struct GfxTerrainViewMode GfxTerrainViewMode;
typedef struct HeightMap HeightMap;
typedef struct TerrainBuffer TerrainBuffer;

void gfxHeightMapUseVisualizationSoilDepth(GfxTerrainViewMode *view_params, HeightMap *height_map, 
										   TerrainBuffer *buffer, int lod,
										   U8 *data, int display_size);

void gfxHeightMapUseVisualizationMaterialWeight(TerrainEditorSourceLayer *layer, GfxTerrainViewMode *view_params,
												HeightMap *height_map, TerrainBuffer *buffer, 
												int lod, U8 *data, int display_size);

void gfxHeightMapUseVisualizationObjectDensity(TerrainEditorSourceLayer *layer, GfxTerrainViewMode *view_params,
											   HeightMap *height_map, TerrainBuffer *buffer, 
											   int lod, U8 *data, int display_size);

void gfxHeightMapUseVisualizationGrid(GfxTerrainViewMode *view_params, HeightMap *height_map, 
									  int lod, U8 *data, int display_size);

void gfxHeightMapUseVisualizationExtremeAngles(GfxTerrainViewMode *view_params, HeightMap *height_map, 
											   TerrainBuffer *buffer, int lod,
											   U8 *data, int display_size);

void gfxHeightMapUseVisualizationSelection(GfxTerrainViewMode *view_params, HeightMap *height_map, 
										   TerrainBuffer *buffer, int lod,
										   U8 *data, int display_size);

void gfxHeightMapUseVisualizationShadow(GfxTerrainViewMode *view_params, HeightMap *height_map, 
										TerrainBuffer *buffer, int lod,
										U8 *data, int display_size);

void gfxHeightMapUseVisualizationDesignAttr(GfxTerrainViewMode *view_params, HeightMap *height_map, 
										   TerrainBuffer *buffer, int lod,
										   U8 *data, int display_size);
