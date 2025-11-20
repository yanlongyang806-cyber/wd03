//// WorldLib UGC functions
////
//// The only parts of UGC that exist in WorldLib are those that are
//// stored in the .zeni files.  At the moment, that is only
//// restrictions.
#include "wlUGC.h"

#include "WorldGridLoadPrivate.h"
#include "WorldGridPrivate.h"
#include "utilitiesLib.h"
#include "wlExclusionGrid.h"
#include "wlState.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

//////////////////////////////////////////////
// UGC Layer Data Cache
//////////////////////////////////////////////

LibFileLoad **g_UGCLayerCache = NULL;
char **g_UGCSkyCache = NULL;
DictionaryHandle g_UGCPlatformInfoDict = NULL;

AUTO_RUN;
void ugcPlatformDictionaryInit(void)
{
	g_UGCPlatformInfoDict = RefSystem_RegisterSelfDefiningDictionary(UGC_PLATFORM_INFO_DICT, false, parse_UGCMapPlatformData, true, true, NULL);
}


void ugcLayerCacheClear(void)
{
	eaDestroyStruct(&g_UGCLayerCache, parse_LibFileLoad);
	eaDestroyEx(&g_UGCSkyCache, NULL);
}

void ugcLayerCacheWriteLayer(ZoneMapLayer *layer)
{
	int i;
	LibFileLoad *lib = StructCreate(parse_LibFileLoad);
	GroupDef **lib_defs = groupLibGetDefEArray(layer->grouptree.def_lib);
	lib->filename = allocAddFilename(layer->filename);
	for (i = 0; i < eaSize(&lib_defs); i++)
	{
		groupFixupBeforeWrite(lib_defs[i], true);
		eaPush(&lib->defs, StructClone(parse_GroupDef, lib_defs[i]));
	}
	eaPush(&g_UGCLayerCache, lib);
}

LibFileLoad *ugcLayerCacheGetLayer(const char *filename)
{
	FOR_EACH_IN_EARRAY(g_UGCLayerCache, LibFileLoad, lib)
	{
		if (stricmp(lib->filename, filename) == 0)
			return lib;
	}
	FOR_EACH_END;
	return NULL;
}

LibFileLoad **ugcLayerCacheGetAllLayers(void)
{
	return g_UGCLayerCache;
}

void ugcLayerCacheAddLayerData(LibFileLoad *layer_data)
{
	eaPush(&g_UGCLayerCache, layer_data);
}

void ugcLayerCacheWriteSky(char *sky_def)
{
	eaPush(&g_UGCSkyCache, strdup(sky_def));
}

char **ugcLayerCacheGetAllSkies(void)
{
	return g_UGCSkyCache;
}

//////////////////////////////////////////////
// Utility
//////////////////////////////////////////////

void ugcZoneMapLayerSaveUGCData(ZoneMap *zmap)
{
	char base_dir[MAX_PATH], out_dir[MAX_PATH];
	ExclusionVolumeGroup **groups = NULL;	
	UGCMapPlatformData *map_data = StructCreate(parse_UGCMapPlatformData);

	map_data->pcMapName = zmapGetName(zmap);

	// Get all the platform volumes
	FOR_EACH_IN_EARRAY(zmap->layers, ZoneMapLayer, layer)
	{
		exclusionGetDefVolumeGroups(layer->grouptree.root_def, &groups, false, -1);
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(groups, ExclusionVolumeGroup, volume_group)
	{
		UGCMapPlatformGroup *group = NULL;
		FOR_EACH_IN_EARRAY(volume_group->volumes, ExclusionVolume, volume)
		{
			if (volume->platform_type)
			{
				ExclusionVolume *volume_st = StructClone(parse_ExclusionVolume, volume);
				if (!group)
				{
					group = StructCreate(parse_UGCMapPlatformGroup);
					copyMat4(volume_group->mat_offset, group->mat);
				}
				eaPush(&group->volumes, volume_st);
			}
		}
		FOR_EACH_END;
		if (group)
			eaPush(&map_data->platform_groups, group);
	}
	FOR_EACH_END;
	eaDestroyEx(&groups, exclusionVolumeGroupDestroy);

	worldGetTempBaseDir(zmap->map_info.filename, SAFESTR(base_dir));
	sprintf(out_dir, "%s/ugc.platforms", base_dir);
	ParserWriteTextFileFromSingleDictionaryStruct(out_dir, UGC_PLATFORM_INFO_DICT, map_data, 0, 0);
	binNotifyTouchedOutputFile(out_dir);
	StructDestroy(parse_UGCMapPlatformData, map_data);
}

void ugcZoneMapLayerTouchUGCData(ZoneMap *zmap)
{
	char base_dir[MAX_PATH], out_dir[MAX_PATH];
	worldGetTempBaseDir(zmap->map_info.filename, SAFESTR(base_dir));
	sprintf(out_dir, "%s/ugc.platforms", base_dir);
	binNotifyTouchedOutputFile(out_dir);
}

#include "wlUGC_h_ast.c"

