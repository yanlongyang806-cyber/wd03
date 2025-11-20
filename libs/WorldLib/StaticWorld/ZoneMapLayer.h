/***************************************************************************



***************************************************************************/

#ifndef _ZONEMAPLAYER_H_
#define _ZONEMAPLAYER_H_
GCC_SYSTEM

#include "grouputil.h"
#include "wlTerrainEnums.h"
#include "WorldLibEnums.h"

typedef struct GroupDef GroupDef;
typedef struct GroupTracker GroupTracker;
typedef struct GroupDefLib GroupDefLib;
typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct TerrainBlockRange TerrainBlockRange;
typedef struct TerrainBuffer TerrainBuffer;
typedef struct Packet Packet;
typedef struct TerrainBuffer TerrainBuffer;
typedef struct EventInfo EventInfo;
typedef struct WorldRegion WorldRegion;
typedef struct HeightMapAtlas HeightMapAtlas;
typedef struct TerrainFileDef TerrainFileDef;
typedef struct WorldCellEntry WorldCellEntry;
typedef struct TerrainObjectEntry TerrainObjectEntry;
typedef struct TerrainMaterialChangeList TerrainMaterialChangeList;
typedef struct HeightMapBackupList HeightMapBackupList;

#define BACKUP_TIME_TO_KEEP (60*60*24*14) //	 in seconds

SA_RET_NN_VALID ZoneMapLayer *layerNew(SA_PARAM_OP_VALID ZoneMap *zmap, SA_PARAM_OP_STR const char *layer_filename);
bool layerNeedsTerrainBins(ZoneMapLayer *layer);
void layerLoadStreaming(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_OP_STR const char *layer_name);
void layerLoadGroupSource(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_OP_STR const char *layer_name, bool binning);
void layerUnload(SA_PARAM_NN_VALID ZoneMapLayer *layer);
void layerReload(SA_PARAM_NN_VALID ZoneMapLayer *layer, bool load_terrain);
void layerClear(SA_PARAM_NN_VALID ZoneMapLayer *layer);
ZoneMapLayerMode layerGetMode(ZoneMapLayer *layer);
ZoneMapLayerMode layerGetTargetMode(ZoneMapLayer *layer);
void layerSetMode(ZoneMapLayer *layer, ZoneMapLayerMode mode, bool reload_game_data, bool binning, bool asynchronous);
bool layerGetUnsaved(SA_PARAM_NN_VALID ZoneMapLayer *layer);
void layerSetUnsaved(SA_PARAM_NN_VALID ZoneMapLayer *layer, bool unsaved);
bool layerSaveAs(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_OP_VALID const char *filename, bool force, bool asynchronous, bool save_terrain, bool fixup_messages);
bool layerSave(SA_PARAM_NN_VALID ZoneMapLayer *layer, bool force, bool asynchronous);
void layerFree(SA_PRE_OP_VALID SA_POST_P_FREE ZoneMapLayer *layer);
bool layerIsSaving(ZoneMapLayer *layer);
void layerSetHACKDisableGameCallbacks(bool value);

bool layerIsReference(SA_PARAM_NN_VALID const ZoneMapLayer *layer);
const char *layerGetFilename(SA_PARAM_NN_VALID const ZoneMapLayer *layer);
const char *layerGetName(SA_PARAM_NN_VALID const ZoneMapLayer *layer);
void layerChangeFilename(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_STR const char *filename);
void getTerrainSourceFilename(char *filename_out, int filename_out_size, const TerrainFileDef *def, TerrainBlockRange *block, const IVec2 pos, ZoneMapLayer *layer);
void layerGetHeaderBinFile(char *filename, int filename_size, ZoneMapLayer *layer);
void layerGetGenesisDir(char *filename, int filename_size, ZoneMapLayer *layer);

void layerUpdateBounds(SA_PARAM_NN_VALID ZoneMapLayer *layer);
void layerGetBounds(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 local_min, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 local_max);
void layerGetTerrainBounds(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 local_min, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 local_max);
void layerGetVisibleBounds(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 local_min, SA_PRE_NN_ELEMS(3) SA_POST_OP_VALID Vec3 local_max);

SA_ORET_NN_VALID ZoneMap *layerGetZoneMap(SA_PARAM_NN_VALID ZoneMapLayer *layer);
int layerIdxInParent(SA_PARAM_OP_VALID ZoneMapLayer *layer);
ZoneMapLayer *layerByFilename(SA_PARAM_NN_VALID ZoneMap *zmap, SA_PARAM_NN_STR const char *filename);

void layerSetWorldRegion(ZoneMapLayer *layer, SA_PARAM_OP_VALID const char *region_name);
SA_ORET_NN_VALID WorldRegion *layerGetWorldRegion(SA_PARAM_OP_VALID ZoneMapLayer *layer);
SA_ORET_OP_STR const char *layerGetWorldRegionString(SA_PARAM_OP_VALID ZoneMapLayer *layer);

void layerHide(SA_PARAM_NN_VALID ZoneMapLayer *layer, bool hide);
void layerHideTerrain(SA_PARAM_NN_VALID ZoneMapLayer *layer, bool hide);

typedef struct GathererData
{
	StashTable textures, geos;
	EventInfo **events;
} GathererData;

void layerGetTexturesAndGeosAndEvents(ZoneMapLayer *layer, GathererData *gather_data);

bool layerIsGenesis(SA_PARAM_NN_VALID ZoneMapLayer *layer);
// Returns true if all files were successfully checked out (if query_only is set, if all files *could* be successfully checked out)
bool layerAttemptLock(ZoneMapLayer *layer, char *lockee_buf, int lockee_size, bool query_only);
U32 layerGetLocked(SA_PARAM_NN_VALID ZoneMapLayer *layer);
void layerSetLocked(SA_PARAM_NN_VALID ZoneMapLayer *layer, U32 locked);
void layerIncrementModTime(SA_PARAM_NN_VALID ZoneMapLayer *layer);
void layerTrackerUpdate(SA_PARAM_OP_VALID ZoneMapLayer *layer, bool force, bool terrain_only);
void layerTrackerOpen(SA_PARAM_OP_VALID ZoneMapLayer *layer);
void layerTrackerClose(SA_PARAM_OP_VALID ZoneMapLayer *layer);
bool layerFindCellEntry(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PARAM_NN_VALID WorldCellEntry *entry);
void layerCreateCellEntries(SA_PARAM_OP_VALID ZoneMapLayer *layer);
void layerGroupTreeTraverse(SA_PARAM_OP_VALID ZoneMapLayer *layer, GroupTreeTraverserCallback callback, void *user_data, bool in_editor, bool terrain_objects);

char **layerGetDependentFileNames(SA_PARAM_OP_VALID ZoneMapLayer *layer);

// GroupTree type
SA_RET_OP_VALID GroupDefLib *layerGetGroupDefLib(SA_PARAM_OP_VALID ZoneMapLayer *layer);
SA_RET_OP_VALID GroupTracker *layerGetTracker(SA_PARAM_OP_VALID ZoneMapLayer *layer);
SA_RET_OP_VALID GroupDef *layerGetDef(SA_PARAM_OP_VALID ZoneMapLayer *layer);
void layerResetRootGroupDef(SA_PARAM_OP_VALID ZoneMapLayer *layer, bool make_tracker);

// Terrain type
bool terrainStartSavingBlock(TerrainBlockRange *block);
bool terrainDoneSavingBlock(TerrainBlockRange *block);
bool terrainCheckoutBlock(TerrainBlockRange *block);
bool terrainCheckoutBlocks(TerrainBlockRange **block);
bool terrainCheckoutLayer(ZoneMapLayer *layer, char *lockee_buf, int lockee_size, bool query_only);

bool terrainDeleteBlock(TerrainBlockRange *block);

bool layerGetPlayable(SA_PARAM_NN_VALID ZoneMapLayer *layer);
void layerSetPlayable(SA_PARAM_NN_VALID ZoneMapLayer *layer, bool playable);

TerrainExclusionVersion layerGetExclusionVersion(SA_PARAM_NN_VALID ZoneMapLayer *layer);
F32 layerGetColorShift(SA_PARAM_NN_VALID ZoneMapLayer *layer);

HeightMapAtlas **layerGetAtlasList(SA_PARAM_NN_VALID ZoneMapLayer *layer, int block);

int layerGetTerrainBlockCount(ZoneMapLayer *layer);
SA_RET_OP_VALID TerrainBlockRange *layerGetTerrainBlock(SA_PARAM_NN_VALID ZoneMapLayer *layer, int block_idx);
int layerGetTerrainBlockForLocalPos(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PRE_NN_ELEMS(2) SA_POST_OP_VALID IVec2 local_pos);
bool layerIsOverlappingTerrainBlock(SA_PARAM_NN_VALID ZoneMapLayer *layer, SA_PRE_NN_ELEMS(2) SA_POST_OP_VALID IVec2 local_pos, IVec2 size);
bool layerGetTerrainBlockExtents(ZoneMapLayer *layer, int block_idx, IVec2 min, IVec2 max);
const char *layerGetTerrainObjectEntryName(TerrainObjectEntry *object_info);

TerrainBuffer *layerCreateTerrainBuffer(SA_PARAM_NN_VALID ZoneMapLayer *layer, TerrainBufferType type, int x, int y, int width, int height, int lod);
TerrainBuffer *layerGetTerrainBuffer(SA_PARAM_NN_VALID ZoneMapLayer *layer, TerrainBufferType type, int lod);
void layerDeleteTerrainBuffer(SA_PARAM_NN_VALID ZoneMapLayer *layer, TerrainBufferType type, int lod);
void layerTerrainUpdateFilenames(ZoneMapLayer *layer, TerrainBlockRange *block);

TerrainBuffer *layerGetOrCreateTerrainBuffer(SA_PARAM_NN_VALID ZoneMapLayer *layer, TerrainBufferType type);

U32 layerGetLoadedNormalLOD(SA_PARAM_NN_VALID ZoneMapLayer *layer);

void layerLoadTerrainObjects(SA_PARAM_NN_VALID ZoneMapLayer *layer);

void layerUpdateGeometry(SA_PARAM_NN_VALID ZoneMapLayer *layer, bool update_trackers);

bool layerTerrainAddBlock(ZoneMapLayer *layer, IVec2 min_block, IVec2 max_block);
void updateTerrainBlocks(ZoneMapLayer *layer);
void initTerrainBlocks(ZoneMapLayer *layer, bool from_bins);

void deinitTerrainBlock(TerrainBlockRange *block);

void layerReloadTerrainBins(ZoneMap *zmap, ZoneMapLayer *layer);

void layerCheckUpdatePathNodeTrackers();

#endif //_ZONEMAPLAYER_H_

