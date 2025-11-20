#pragma once
GCC_SYSTEM

#if !defined(GENESIS_ALLOW_OLD_HEADERS)
#error Including this file was probably a mistake.  UGC should include the appropriate gslUGC*.h files.
#endif

#ifndef NO_EDITORS

#include "wlTerrainQueue.h"
#include "referencesystem.h"
#include "wlGenesis.h"

typedef struct TerrainTaskQueue TerrainTaskQueue;
typedef struct TerrainEditorSource TerrainEditorSource;
typedef struct GenesisGeotype GenesisGeotype;
typedef struct GenesisToPlaceState GenesisToPlaceState;

#define GENESIS_ECOTYPE_DICTIONARY "GenesisEcosystem"

AUTO_STRUCT;
typedef struct GenesisEcotype
{
	char *brush_name;						AST(NAME("BrushName"))
	char **attribute_types;					AST(NAME("AttributeTypes"))
} GenesisEcotype;

AUTO_STRUCT;
typedef struct GenesisEcosystem
{
	const char	*filename;					AST(NAME("Filename") CURRENTFILE  USERFLAG(TOK_USEROPTIONBIT_1))
	char *name;								AST(NAME("Name") KEY  USERFLAG(TOK_USEROPTIONBIT_1))
	char *tags;								AST(NAME("Tags"))
	GenesisSoundInfo *sound_info;			AST(NAME("SoundInfo"))
	char *water_name;						AST(NAME("WaterPlane"))
	char *path_geometry;					AST(NAME("PathGeometry"))
	F32 clearing_size;						AST(NAME("ClearingSize"))
	F32 clearing_falloff;					AST(NAME("ClearingFalloff"))
	GroupDefRef **placed_objects;			AST(NAME("JustPlacedObject"))
	GenesisEcotype **ecotypes;				AST(NAME("Ecotype"))
	TerrainExclusionVersion exclusion_version; AST(NAME("ExclusionVersion") DEFAULT(1))
} GenesisEcosystem;
extern ParseTable parse_GenesisEcosystem[];
#define TYPE_parse_GenesisEcosystem GenesisEcosystem

AUTO_STRUCT;
typedef struct GenesisTerrainAttribute
{
	char *name;								AST(NAME("Name"))
	U8 group_num;							AST(NAME("GroupNumber"))
	U8 table_idx;							NO_AST
} GenesisTerrainAttribute;
extern ParseTable parse_GenesisTerrainAttribute[];
#define TYPE_parse_GenesisTerrainAttribute GenesisTerrainAttribute

AUTO_STRUCT;
typedef struct GenesisDesignData
{
	GenesisTerrainAttribute **attribute_list;
} GenesisDesignData;
extern ParseTable parse_GenesisDesignData[];
#define TYPE_parse_GenesisDesignData GenesisDesignData

void genesisLoadEcotypeLibrary();
U8 terGenGetAttributeIdx(GenesisDesignData *data, GenesisTerrainAttribute *in_attr);
void genesisMoveDesignToDetail(TerrainTaskQueue *queue, TerrainEditorSource *source,
								GenesisEcosystem *ecosystem, int flags, terrainTaskCustomFinishCallback callback, void *userdata);
void terEdApplyBrushToTerrain(TerrainTaskQueue *queue, TerrainEditorSource *source, const char *name, bool optimized, bool locked_edges, int flags);
void genesisDoDrawSelection(TerrainEditorSource *source, GenesisEcotype *ecotype);
void genesisMakeDetailObjects(GenesisToPlaceState *to_place, GenesisEcosystem *ecosystem, GenesisZoneNodeLayout *layout, bool force_no_water, bool make_sky_volumes);

#endif
