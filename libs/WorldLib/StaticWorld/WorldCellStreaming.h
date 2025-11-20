#pragma once
GCC_SYSTEM
/***************************************************************************



***************************************************************************/

#ifndef __WORLDCELLSTREAMING_H__
#define __WORLDCELLSTREAMING_H__

typedef struct BinFileList BinFileList;
typedef struct BinFileListWithCRCs BinFileListWithCRCs;
typedef struct WorldCell WorldCell;
typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapInfo ZoneMapInfo;
typedef struct WorldRegion WorldRegion;
typedef struct BlockRange BlockRange;

typedef enum BinFileListType
{
	BFLT_TERRAIN_BIN,
	BFLT_TERRAIN_ATLAS,
	BFLT_WORLD_CELL,
	BFLT_EXTERNAL,
	BFLT_COUNT,
} BinFileListType;

bool worldCellCheckNeedsBins(ZoneMapInfo *zminfo);
void worldCellLoadBins(ZoneMap *zmap, bool force_create_bins, bool load_from_source, bool is_secondary);
void worldCellSetEditable(void);

void worldCellQueueLoad(WorldCell *cell, bool foreground_load);
void worldCellCancelLoad(WorldCell *cell);
void worldCellFinishLoad(WorldCell *cell);
void worldCellForceLoad(WorldCell *cell);

void worldCellQueueWeldedBinLoad(WorldCell *cell);
void worldCellCancelWeldedBinLoad(WorldCell *cell);
void worldCellFinishWeldedBinLoad(WorldCell *cell);
void worldCellForceWeldedBinLoad(WorldCell *cell);

void bflFreeManifestCache(void);
U32 bflFileLastChanged(const char *filename);
U32 bflGetVersionTimestamp(BinFileListType type);
void bflAddSourceFile(BinFileList *file_list, const char *filename_in);
void bflAddOutputFileEx(BinFileList *file_list, const char *filename_in, bool dont_rebuild_if_doesnt_exist);
#define bflAddOutputFile(file_list, filename) bflAddOutputFileEx(file_list, filename, false)
bool bflUpdateOutputFile(const char *filename_out);
bool bflFixupAndWrite(BinFileList *file_list, const char *filename, BinFileListType type);

void bflAddDepsSourceFile(BinFileListWithCRCs *file_list, const char *filename_in);
void bflSetDepsEncounterLayerCRC(BinFileListWithCRCs *file_list, U32 crc);
void bflSetDepsGAELayerCRC(BinFileListWithCRCs *file_list, U32 crc);

void worldSetEncounterLayerCRC(U32 crc);
void worldSetGAELayerCRC(U32 crc);

U32 worldCellGetOverrideRebinCRC(void);
void worldCellSetupChildCellRange(WorldCell *cell_dst, BlockRange *child_range, IVec3 size, int index);

#endif //__WORLDCELLSTREAMING_H__

