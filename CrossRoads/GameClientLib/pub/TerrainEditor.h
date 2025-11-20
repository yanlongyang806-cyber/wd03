// TerrainEditor.h

#pragma once
GCC_SYSTEM

#ifndef NO_EDITORS

#include "stdtypes.h"
#include "WorldLibEnums.h"

#define TERRAIN_MAX_MEMORY (1024*1024*1600) // ~1.6 Gbytes

typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct TerrainDoc TerrainDoc;
typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;

void terEdOncePerFrame( F32 delta_time );
void terEdWaitForQueuedEvents(TerrainDoc *doc);

void terEdMarkDirty(void);

void terrainBakeTrackerIntoTerrain(GroupTracker *tracker);

void terrainUpdateTerrainObjectsByDef(GroupDef *changed_def);

void terrainEditorLayerModeChanged(ZoneMapLayer *layer, ZoneMapLayerMode mode, bool asynchronous);
bool terrainEditorSaveSourceData(ZoneMapLayer *layer, bool force, bool asynchronous);
void terrainEditorUpdateSourceDataFilenames(ZoneMapLayer *layer);
bool terrainEditorAddSourceData(ZoneMapLayer *layer, IVec2 min, IVec2 max);

char* terEdGetSelectedBrushName();

#endif
