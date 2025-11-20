//// WorldLib UGC functions
////
//// The only parts of UGC that exist in WorldLib are those that are
//// stored in the .zeni files.  At the moment, that is only
//// restrictions.
#pragma once
GCC_SYSTEM

typedef struct ExclusionVolume ExclusionVolume;
typedef struct LibFileLoad LibFileLoad;
typedef struct WorldVariableDef WorldVariableDef;
typedef struct ZoneMap ZoneMap;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef const void* DictionaryHandle;

extern DictionaryHandle g_UGCPlatformInfoDict;

AUTO_STRUCT;
typedef struct WorldUGCFactionRestrictionProperties
{
	const char* pcFaction;												AST( NAME(Faction) STRUCTPARAM POOL_STRING )
} WorldUGCFactionRestrictionProperties;
extern ParseTable parse_WorldUGCFactionRestrictionProperties[];
#define TYPE_parse_WorldUGCFactionRestrictionProperties WorldUGCFactionRestrictionProperties

AUTO_STRUCT;
typedef struct WorldUGCRestrictionProperties
{
	S32 iMinLevel;														AST( NAME(MinLevel) )
	S32 iMaxLevel;														AST( NAME(MaxLevel) )

	// or'd fields
	WorldUGCFactionRestrictionProperties** eaFactions;					AST( NAME(Faction, RestrictAllegiance) )

	// and'd fields
	// NONE YET;
} WorldUGCRestrictionProperties;
extern ParseTable parse_WorldUGCRestrictionProperties[];
#define TYPE_parse_WorldUGCRestrictionProperties WorldUGCRestrictionProperties

//// UGC Layer Data Cache

void ugcLayerCacheClear(void);
void ugcLayerCacheWriteLayer(ZoneMapLayer *layer);
LibFileLoad *ugcLayerCacheGetLayer(const char *filename);
LibFileLoad **ugcLayerCacheGetAllLayers(void);
void ugcLayerCacheWriteSky(char *sky_def);
char **ugcLayerCacheGetAllSkies(void);
void ugcLayerCacheAddLayerData(LibFileLoad *layer_data);

//// Platforms
#define UGC_PLATFORM_INFO_DICT "UGCPlatformInfo"

AUTO_STRUCT;
typedef struct UGCMapPlatformGroup
{
	Mat4 mat;
	ExclusionVolume **volumes;
} UGCMapPlatformGroup;
extern ParseTable parse_UGCMapPlatformGroup[];
#define TYPE_parse_UGCMapPlatformGroup UGCMapPlatformGroup

AUTO_STRUCT;
typedef struct UGCMapPlatformData
{
	const char *pcMapName;					AST(KEY POOL_STRING)
	UGCMapPlatformGroup **platform_groups;
} UGCMapPlatformData;
extern ParseTable parse_UGCMapPlatformData[];
#define TYPE_parse_UGCMapPlatformData UGCMapPlatformData

void ugcZoneMapLayerSaveUGCData(ZoneMap *zmap);
void ugcZoneMapLayerTouchUGCData(ZoneMap *zmap);
