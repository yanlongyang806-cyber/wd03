#include "wlVolumes.h"
#include "wlVolumes_h_ast.h"

#include "Capsule.h"
#include "FolderCache.h"
#include "GenericMesh.h"
#include "MemoryBudget.h"
#include "Quat.h"
#include "StringCache.h"
#include "fileutil.h"
#include "qsortG.h"
#include "RoomConn.h"
#include "WorldGridPrivate.h"
#include "timing.h"
#include "Octree.h"
#include "wlState.h"
#include "bounds.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););
AUTO_RUN_ANON(memBudgetAddMapping("WorldVolume Octree (all)", BUDGET_World););

U32 s_EntityVolumeQueryType=0;

typedef enum WorldVolumeQueryType
{
	WL_QUERY_BOX,
	WL_QUERY_SPHERE,
	WL_QUERY_CAPSULE,
} WorldVolumeQueryType;

typedef struct WorldVolume
{
	int iPartitionIdx;

	WorldVolumeElement **elements;

	F32 volume_size; // used for sorting

	OctreeEntry **octree_entries;

	// user data
	U32 volume_type;
	void *volume_data;

	// cached queries
	WorldVolumeQueryCache **caches;

	U32 last_raycollide_time;

	WorldVolumeQueryCacheChangeCallback query_entered_callback, query_exited_callback;
	WorldVolumeQueryCacheChangeCallback remain_inside_callback;	// Called while the entity stays inside the volume
	WorldVolumeDataFreeCallback data_free_callback;

	MEM_DBG_STRUCT_PARMS
} WorldVolume;

typedef struct WorldVolumeQueryCache
{
	int iPartitionIdx;

	// cached results
	WorldVolume **prev_volumes;
	WorldVolume **volumes;

	// user data
	U32 query_type;
	void *query_data;

	// per query data
	U32 query_volume_type;

	// cached data
	U32 last_query_mod_time;
	WorldVolumeQueryType last_query_type;
	Mat4 world_mat, inv_world_mat;
	Vec3 local_min, local_max, world_mid;
	Vec3 cap_start, cap_dir;
	F32 radius, length;

	// callbacks
	WorldVolumeQueryCacheChangeCallback query_entered_callback, query_exited_callback;
	WorldVolumeQueryCacheChangeCallback remain_inside_callback;	// Called while the entity stays inside the volume

	MEM_DBG_STRUCT_PARMS
} WorldVolumeQueryCache;

typedef struct WorldVolumePartitionData {
	Octree **eaVolumeOctrees;
	Octree *pAllTypeVolumeOctree;
	U32 uVolumeModTime;
} WorldVolumePartitionData;

WorldVolumePartitionData **eaVolumeData;
static U32 all_volume_types;

U32 volume_type_bit_masks[2] = {0xffffffff, 0xffffffff};

/// Unique name for the Water dictionary
#define WATER_DATA_DICTIONARY "WATER"

/// Where all the water files are stored
#define WATER_DATA_PATH "environment/water/"

/// Dictionary containing WorldVolumeWater data objects
static DictionaryHandle water_data_dict;

static void reloadWater( const char* relpath, int when )
{
	loadstart_printf( "Reloading Water volume file..." );

	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, water_data_dict );
	wl_state.water_reloaded_this_frame = true;

	loadend_printf( "done" );
}

/// Initialize the volume library.
void wlVolumeStartup(void)
{
    int water_data_count = 0;
    
    loadstart_printf("Loading water volume defs...");

    water_data_dict = RefSystem_RegisterSelfDefiningDictionary( WATER_DATA_DICTIONARY, false, parse_WorldVolumeWater, true, false, NULL );
    ParserLoadFilesToDictionary( WATER_DATA_PATH, ".h2o", "water.bin", PARSER_BINS_ARE_SHARED | PARSER_OPTIONALFLAG, water_data_dict );
    water_data_count = RefSystem_GetDictionaryNumberOfReferents( water_data_dict );
	if (isDevelopmentMode())
		FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, WATER_DATA_PATH "*.h2o", reloadWater );

    loadend_printf( "done (%d volumes).", water_data_count );

	if (!(wl_state.load_flags & WL_NO_LOAD_MATERIALS))
	{
		loadstart_printf( "Verifying water volume defs..." );
		FOR_EACH_IN_REFDICT( WATER_DATA_DICTIONARY, WorldVolumeWater, water) {
			if( !materialExists( water->materialName )) {
				ErrorFilenamef( water->filename, "Water volume references nonexistent material %s", water->materialName );
			}
			if( !materialExists( water->lowEnd.materialNearSurface )) {
				ErrorFilenamef( water->filename, "Water volume references nonexistent material %s", water->lowEnd.materialNearSurface );
			}
			if( water->lowEnd.waterColorHSV[0] == 0 && water->lowEnd.waterColorHSV[1] == 0 && water->lowEnd.waterColorHSV[2] == 0 ) {
				ErrorFilenamef( water->filename,
								"Water volume does not specify any low end "
								"color.  If you want pure black,  just set "
								"hue to non-zero." );
			}
		} FOR_EACH_END;
		loadend_printf( " done." );
	}
}

void wlVolumeCreatePartition(int iPartitionIdx)
{
	WorldVolumePartitionData *pData;

	PERFINFO_AUTO_START_FUNC();
	
	pData = eaGet(&eaVolumeData, iPartitionIdx);
	if (!pData) {
		// Create the partition data
		pData = calloc(1,sizeof(WorldVolumePartitionData));
		eaSet(&eaVolumeData, pData, iPartitionIdx);

		// Init Rooms
		roomConnGraphCreatePartition(world_grid.active_map, iPartitionIdx);
	}

	PERFINFO_AUTO_STOP();
}

void wlVolumeDestroyPartition(int iPartitionIdx)
{
	WorldVolumePartitionData *pData;

	PERFINFO_AUTO_START_FUNC();
	
	pData = eaGet(&eaVolumeData, iPartitionIdx);
	if (pData) {
		// Deinit Rooms
		roomConnGraphDestroyPartition(world_grid.active_map, iPartitionIdx);

		// Free the partition data
		octreeDestroy(pData->pAllTypeVolumeOctree);
		eaDestroyEx(&pData->eaVolumeOctrees, octreeDestroy);
		free(pData);

		eaSet(&eaVolumeData, NULL, iPartitionIdx);
	}

	PERFINFO_AUTO_STOP();
}

bool wlVolumePartitionExists(int iPartitionIdx)
{
	return (eaGet(&eaVolumeData, iPartitionIdx) != NULL);
}

int wlVolumeMaxPartitionIndex(void)
{
	return (eaSize(&eaVolumeData) - 1);
}

static WorldVolumePartitionData *wlVolumeGetPartitionData(int iPartitionIdx)
{
	WorldVolumePartitionData *pData = eaGet(&eaVolumeData, iPartitionIdx);
	assertmsgf(pData, "Partition %d does not exist", iPartitionIdx);
	return pData;
}

/// Return the list of all the water def keys
const char*** wlVolumeWaterDefKeys( const char*** eaBuffer )
{
	RefDictIterator it;
    Referent ref;

    eaClear( eaBuffer );
    RefSystem_InitRefDictIterator( WATER_DATA_DICTIONARY, &it );
	eaPush( eaBuffer, "Air" );
    while( ref = RefSystem_GetNextReferentFromIterator( &it )) {
        const char* key = RefSystem_StringFromReferent( ref );
        
        eaPush( eaBuffer, key );
    }

	eaQSortG( *eaBuffer, strCmp );

    return eaBuffer;
}

/// Return the water volume object named by KEY
WorldVolumeWater* wlVolumeWaterFromKey( const char* key )
{
    if (key == NULL || key[0] == '\0' || stricmp( key, "Air" )==0 )
        return NULL;
    else
        return RefSystem_ReferentFromString( WATER_DATA_DICTIONARY, key );
}

/// Return if any water definitions were reloaded this frame. 
bool wlVolumeWaterReloadedThisFrame(void)
{
	return wl_state.water_reloaded_this_frame;
}

/// Call this once at the end of a frame
void wlVolumeWaterClearReloadedThisFrame(void)
{
	wl_state.water_reloaded_this_frame = false;
}

static void wlVolumeRemoveQueryCache(WorldVolume *volume, WorldVolumeQueryCache *query_cache)
{
	PERFINFO_AUTO_START_FUNC();

	eaFindAndRemoveFast(&volume->caches, query_cache);
	if (volume->query_exited_callback)
		volume->query_exited_callback(volume, query_cache);
	if (query_cache->query_exited_callback)
		query_cache->query_exited_callback(volume, query_cache);

	PERFINFO_AUTO_STOP();
}

static void wlVolumeAddQueryCache(WorldVolume *volume, WorldVolumeQueryCache *query_cache)
{
	PERFINFO_AUTO_START_FUNC();

	steaPush(&volume->caches, query_cache, volume);
	if (volume->query_entered_callback)
		volume->query_entered_callback(volume, query_cache);
	if (query_cache->query_entered_callback)
		query_cache->query_entered_callback(volume, query_cache);

	PERFINFO_AUTO_STOP();
}

static void wlVolumeQueryCacheRemoveVolume(WorldVolumeQueryCache *query_cache, WorldVolume *volume)
{
	eaFindAndRemoveFast(&query_cache->volumes, volume);
	if (wl_state.HACK_disable_game_callbacks)
		return;
	
	if (volume->query_exited_callback)
		volume->query_exited_callback(volume, query_cache);
	if (query_cache->query_exited_callback)
		query_cache->query_exited_callback(volume, query_cache);
}

static StashTable volume_type_hash;
static StashTable query_type_hash;

static const char *volume_budget_names[32];

U32 wlVolumeTypeNameToBitMask(const char *volume_type_in)
{
	static U32 volume_next_type_bit = 1;
	const char *volume_type;
	int bit = 0, bit_idx;

	volume_type = allocAddString(volume_type_in);

	if (!volume_type_hash)
		volume_type_hash = stashTableCreateWithStringKeys(256, StashDefault);

	if (!stashFindInt(volume_type_hash, volume_type, &bit))
	{
		char volume_budget_name[1024];

		assert(volume_next_type_bit);
		bit = volume_next_type_bit;
		volume_next_type_bit = volume_next_type_bit << 1;
		all_volume_types |= bit;
		stashAddInt(volume_type_hash, volume_type, bit, false);

		bit_idx = log2(bit);
		sprintf(volume_budget_name, "WorldVolume Octree (%s)", volume_type_in);
		volume_budget_names[bit_idx] = allocAddString(volume_budget_name);
		memBudgetAddMapping(volume_budget_names[bit_idx], BUDGET_World);

		// Note: TerrainExclusion is not needed in any octree on either client or server.

		if (stricmp(volume_type_in, "Occluder")==0 ||
			stricmp(volume_type_in, "Water")==0 ||
			stricmp(volume_type_in, "Indoor")==0 ||
			stricmp(volume_type_in, "LightCache")==0 ||
			stricmp(volume_type_in, "FX")==0 ||
			stricmp(volume_type_in, "SkyFade")==0 ||
			stricmp(volume_type_in, "TerrainExclusion")==0 ||
			stricmp(volume_type_in, "IgnoreSound")==0)
		{
			// client only, remove bit from server bit mask
			volume_type_bit_masks[1] &= ~bit;
		}
		
		if (stricmp(volume_type_in, "Power")==0 ||
			stricmp(volume_type_in, "Spawn Point Volume")==0 ||
			stricmp(volume_type_in, "Action")==0 ||
			stricmp(volume_type_in, "Warp")==0 ||
			stricmp(volume_type_in, "Landmark")==0 ||
			stricmp(volume_type_in, "Neighborhood")==0 ||
			stricmp(volume_type_in, "LevelOverride")==0 ||
			stricmp(volume_type_in, "PetsDisabled")==0 ||
			stricmp(volume_type_in, "AI")==0 ||
			stricmp(volume_type_in, "Event")==0 ||
			stricmp(volume_type_in, "Interaction")==0 ||
			stricmp(volume_type_in, "Civilian")==0 ||
			stricmp(volume_type_in, "TerrainExclusion")==0 ||
			stricmp(volume_type_in, "InteractionAttached")==0)
		{
			// server only, remove bit from client bit mask
			volume_type_bit_masks[0] &= ~bit;
		}

	}

	return bit;
}

const char *wlVolumeBitMaskToTypeName(U32 bit)
{
	StashTableIterator iter;
	StashElement elem;

	if (!volume_type_hash)
		return NULL;

	stashGetIterator(volume_type_hash, &iter);
	while (stashGetNextElement(&iter, &elem))
	{
		const char *s = stashElementGetKey(elem);
		int vbit = stashElementGetInt(elem);
		if ((U32)vbit == bit)
			return s;
	}

	return NULL;
}

U32 wlVolumeQueryCacheTypeNameToBitMask(const char *query_type)
{
	static U32 query_next_type_bit = 1;
	int bit = 0;

	query_type = allocAddString(query_type);

	if (!query_type_hash)
		query_type_hash = stashTableCreateWithStringKeys(256, StashDefault);

	if (!stashFindInt(query_type_hash, query_type, &bit))
	{
		assert(query_next_type_bit);
		bit = query_next_type_bit;
		query_next_type_bit = query_next_type_bit << 1;
		stashAddInt(query_type_hash, query_type, bit, false);
	}

	return bit;
}

__forceinline static Octree *getOctree(int iPartitionIdx, U32 volume_type, bool create)
{
	WorldVolumePartitionData *pData;
	int volume_type_idx;

	if (!volume_type)
		return NULL;

	pData = wlVolumeGetPartitionData(iPartitionIdx);

	volume_type_idx = log2(volume_type);
	assert(volume_type == 1 << volume_type_idx);
	assert(volume_type_idx >= 0 && volume_type_idx < 32);
	
	if (create)
	{
		if (eaSize(&pData->eaVolumeOctrees) <= volume_type_idx)
			eaSetSize(&pData->eaVolumeOctrees, volume_type_idx+1);
		ANALYSIS_ASSUME(pData->eaVolumeOctrees);
		if (!pData->eaVolumeOctrees[volume_type_idx])
			pData->eaVolumeOctrees[volume_type_idx] = octreeCreateDebug(NULL, 0, 0, volume_budget_names[volume_type_idx], __LINE__);
	}

	if (volume_type_idx < eaSize(&pData->eaVolumeOctrees))
	{
		ANALYSIS_ASSUME(pData->eaVolumeOctrees);
		return pData->eaVolumeOctrees[volume_type_idx];
	}

	return NULL;
}

static void wlVolumeUpdateElements(WorldVolume *volume)
{
	Vec3 world_min, world_max, world_mid, mn, mx;
	F32 radius;
	int i;

	setVec3same(world_min, 8e16);
	setVec3same(world_max, -8e16);
	volume->volume_size = 0;

	for (i = 0; i < eaSize(&volume->elements); ++i)
	{
		invertMat4Copy(volume->elements[i]->world_mat, volume->elements[i]->inv_world_mat);

		switch (volume->elements[i]->volume_shape)
		{
			xcase WL_VOLUME_BOX:
			{
				volume->elements[i]->face_bits = volume->elements[i]->face_bits ? volume->elements[i]->face_bits : VOLFACE_ALL;
				volume->volume_size += boxCalcSize(volume->elements[i]->local_min, volume->elements[i]->local_max);
				mulBoundsAA(volume->elements[i]->local_min, volume->elements[i]->local_max, volume->elements[i]->world_mat, mn, mx);
			}

			xcase WL_VOLUME_SPHERE:
			{
				volume->volume_size += 1.33333333f * PI * volume->elements[i]->radius * volume->elements[i]->radius * volume->elements[i]->radius;
				addVec3same(volume->elements[i]->world_mat[3], -volume->elements[i]->radius, mn);
				addVec3same(volume->elements[i]->world_mat[3], volume->elements[i]->radius, mx);
			}

			xcase WL_VOLUME_HULL:
			{
				if (!volume->elements[i]->hull)
					volume->elements[i]->hull = gmeshToGConvexHull(volume->elements[i]->mesh);
				volume->volume_size += boxCalcSize(volume->elements[i]->local_min, volume->elements[i]->local_max);
				mulBoundsAA(volume->elements[i]->local_min, volume->elements[i]->local_max, volume->elements[i]->world_mat, mn, mx);
			}
		}

		vec3RunningMin(mn, world_min);
		vec3RunningMax(mx, world_max);
	}

	if (world_min[0] > world_max[0] || world_min[1] > world_max[1] || world_min[2] > world_max[2])
	{
		setVec3same(world_min, 0);
		setVec3same(world_max, 0);
	}

	centerVec3(world_min, world_max, world_mid);

	if (eaSize(&volume->elements) == 1 && volume->elements[0]->volume_shape == WL_VOLUME_SPHERE)
		radius = volume->elements[0]->radius;
	else
		radius = distance3(world_min, world_max) * 0.5f;

	for (i = 0; i < eaSize(&volume->octree_entries); ++i)
	{
		OctreeEntry *octree_entry = volume->octree_entries[i];
		Octree *volume_octree = octree_entry->octree;
		assert(volume_octree);
		octreeRemove(octree_entry);
		copyVec3(world_mid, octree_entry->bounds.mid);
		octree_entry->bounds.radius = radius;
		copyVec3(world_min, octree_entry->bounds.min);
		copyVec3(world_max, octree_entry->bounds.max);
		octree_entry->node = volume;
		octreeAddEntry(volume_octree, octree_entry, OCT_MEDIUM_GRANULARITY);
	}
}

WorldVolume *wlVolumeCreate_dbg(int iPartitionIdx, U32 volume_type, void *volume_data, const WorldVolumeElement **elements MEM_DBG_PARMS)
{
	U32 volume_type_bit, volume_type_idx;
	WorldVolume *volume;
	WorldVolumePartitionData *pData;

	assert(volume_type);

	pData = wlVolumeGetPartitionData(iPartitionIdx);

	volume = scalloc(1, sizeof(WorldVolume));
	MEM_DBG_STRUCT_PARMS_INIT(volume);
	volume->iPartitionIdx = iPartitionIdx;
	volume->volume_data = volume_data;
	volume->volume_type = volume_type;

	eaCopyStructs(&elements, &volume->elements, parse_WorldVolumeElement);

	for (volume_type_bit = 1, volume_type_idx = 0; volume_type_bit <= volume->volume_type; volume_type_bit <<= 1, ++volume_type_idx)
	{
		if (volume->volume_type & volume_type_bit)
		{
			Octree *volume_octree = getOctree(iPartitionIdx, volume_type_bit, true);
			OctreeEntry *octree_entry = stcalloc(1, sizeof(OctreeEntry), volume);
			assert(volume_octree);
			octree_entry->octree = volume_octree;
			steaPush(&volume->octree_entries, octree_entry, volume);
		}
	}

	if (wlIsServer())
	{
		OctreeEntry *octree_entry = stcalloc(1, sizeof(OctreeEntry), volume);
		if (!pData->pAllTypeVolumeOctree)
			pData->pAllTypeVolumeOctree = octreeCreateDebug(NULL, 0, 0, "WorldVolume Octree (all)", __LINE__);
		assert(pData->pAllTypeVolumeOctree);
		octree_entry->octree = pData->pAllTypeVolumeOctree;
		steaPush(&volume->octree_entries, octree_entry, volume);
	}

	wlVolumeUpdateElements(volume);

	++pData->uVolumeModTime;

	return volume;
}

WorldVolume *wlVolumeCreateBox_dbg(int iPartitionIdx, U32 volume_type, void *volume_data, const Mat4 world_mat, const Vec3 local_min, const Vec3 local_max, VolumeFaces face_bits MEM_DBG_PARMS)
{
	WorldVolumePartitionData *pData = wlVolumeGetPartitionData(iPartitionIdx);
	WorldVolume *volume = wlVolumeCreate_dbg(iPartitionIdx, volume_type, volume_data, NULL MEM_DBG_PARMS_CALL);

	eaPush(&volume->elements, StructCreate(parse_WorldVolumeElement));

	volume->elements[0]->volume_shape = WL_VOLUME_BOX;
	copyMat4(world_mat, volume->elements[0]->world_mat);
	copyVec3(local_min, volume->elements[0]->local_min);
	copyVec3(local_max, volume->elements[0]->local_max);
	volume->elements[0]->face_bits = face_bits;

	wlVolumeUpdateElements(volume);

	++pData->uVolumeModTime;

	return volume;
}

WorldVolume *wlVolumeCreateSphere_dbg(int iPartitionIdx, U32 volume_type, void *volume_data, const Mat4 world_mat, const Vec3 local_mid, F32 radius MEM_DBG_PARMS)
{
	WorldVolumePartitionData *pData = wlVolumeGetPartitionData(iPartitionIdx);
	WorldVolume *volume = wlVolumeCreate_dbg(iPartitionIdx, volume_type, volume_data, NULL MEM_DBG_PARMS_CALL);

	eaPush(&volume->elements, StructCreate(parse_WorldVolumeElement));

	volume->elements[0]->volume_shape = WL_VOLUME_SPHERE;
	copyMat3(unitmat, volume->elements[0]->world_mat);
	mulVecMat4(local_mid, world_mat, volume->elements[0]->world_mat[3]);
	volume->elements[0]->radius = radius;

	wlVolumeUpdateElements(volume);

	++pData->uVolumeModTime;

	return volume;
}

WorldVolume *wlVolumeCreateHull_dbg(int iPartitionIdx, U32 volume_type, void *volume_data, const Mat4 world_mat, const Vec3 local_min, const Vec3 local_max, GMesh *mesh, GConvexHull *hull MEM_DBG_PARMS)
{
	WorldVolumePartitionData *pData = wlVolumeGetPartitionData(iPartitionIdx);
	WorldVolume *volume = wlVolumeCreate_dbg(iPartitionIdx, volume_type, volume_data, NULL MEM_DBG_PARMS_CALL);

	eaPush(&volume->elements, StructCreate(parse_WorldVolumeElement));

	volume->elements[0]->volume_shape = WL_VOLUME_HULL;
	copyMat4(world_mat, volume->elements[0]->world_mat);
	copyVec3(local_min, volume->elements[0]->local_min);
	copyVec3(local_max, volume->elements[0]->local_max);
	volume->elements[0]->mesh = mesh;
	volume->elements[0]->hull = hull;
	assert(validateGConvexHull(hull));

	wlVolumeUpdateElements(volume);

	++pData->uVolumeModTime;

	return volume;
}

void wlVolumeUpdateBox(WorldVolume *volume, const Mat4 world_mat, const Vec3 local_min, const Vec3 local_max)
{
	WorldVolumePartitionData *pData = wlVolumeGetPartitionData(volume->iPartitionIdx);
	int i;

	if (eaSize(&volume->elements))
	{
		for (i = 1; i < eaSize(&volume->elements); ++i)
			StructDestroy(parse_WorldVolumeElement, volume->elements[i]);
		eaSetSize(&volume->elements, 1);
	}
	else
	{
		eaPush(&volume->elements, StructCreate(parse_WorldVolumeElement));
	}

	volume->elements[0]->volume_shape = WL_VOLUME_BOX;
	copyMat4(world_mat, volume->elements[0]->world_mat);
	copyVec3(local_min, volume->elements[0]->local_min);
	copyVec3(local_max, volume->elements[0]->local_max);

	wlVolumeUpdateElements(volume);

	++pData->uVolumeModTime;

	for (i = 0; i < eaSize(&volume->caches); ++i)
		wlVolumeQueryCacheRemoveVolume(volume->caches[i], volume);
	eaClear(&volume->caches);
}

void wlVolumeUpdateSphere(WorldVolume *volume, const Mat4 world_mat, const Vec3 local_mid, F32 radius)
{
	WorldVolumePartitionData *pData = wlVolumeGetPartitionData(volume->iPartitionIdx);
	int i;

	if (eaSize(&volume->elements))
	{
		for (i = 1; i < eaSize(&volume->elements); ++i)
			StructDestroy(parse_WorldVolumeElement, volume->elements[i]);
		eaSetSize(&volume->elements, 1);
	}
	else
	{
		eaPush(&volume->elements, StructCreate(parse_WorldVolumeElement));
	}

	volume->elements[0]->volume_shape = WL_VOLUME_SPHERE;
	copyMat3(unitmat, volume->elements[0]->world_mat);
	mulVecMat4(local_mid, world_mat, volume->elements[0]->world_mat[3]);
	volume->elements[0]->radius = radius;

	wlVolumeUpdateElements(volume);

	++pData->uVolumeModTime;

	for (i = 0; i < eaSize(&volume->caches); ++i)
		wlVolumeQueryCacheRemoveVolume(volume->caches[i], volume);
	eaClear(&volume->caches);
}

static void freeOctreeEntry(OctreeEntry *octree_entry)
{
	if (!octree_entry)
		return;
	octreeRemove(octree_entry);
	free(octree_entry);
}

void wlVolumeFree(WorldVolume *volume)
{
	WorldVolumePartitionData *pData;
	int i;

	if (!volume)
		return;

	eaDestroyEx(&volume->octree_entries, freeOctreeEntry);

	pData = wlVolumeGetPartitionData(volume->iPartitionIdx);
	++pData->uVolumeModTime;
	
	for (i = 0; i < eaSize(&volume->caches); ++i)
		wlVolumeQueryCacheRemoveVolume(volume->caches[i], volume);
	eaDestroy(&volume->caches);

	// Free user data if there is any
	if(volume->data_free_callback)
		volume->data_free_callback(volume->volume_data);

	eaDestroyStruct(&volume->elements, parse_WorldVolumeElement);

	free(volume);
}

void *wlVolumeGetVolumeData(const WorldVolume *volume)
{
	return volume->volume_data;
}

U32 wlVolumeGetVolumeType(const WorldVolume *volume)
{
	return volume->volume_type;
}

bool wlVolumeIsType(const WorldVolume *volume, U32 volume_type)
{
	return (volume->volume_type & volume_type) == volume_type;
}

F32 wlVolumeGetSize(const WorldVolume *volume)
{
	return volume->volume_size;
}

F32 wlVolumeGetProgressZ(const WorldVolume *volume, const Vec3 world_point)
{
	Vec3 vPoint;
	Vec3 vLocalMin,vLocalMax;
	Vec3 vMid;
	WorldVolumeElement *element = volume->elements[0];
	wlVolumeGetWorldPosRotMinMax(volume,vMid,NULL,vLocalMin,vLocalMax);

	mulVecMat4(world_point,element->inv_world_mat,vPoint);

	return (vPoint[2]-vLocalMin[2])/(vLocalMax[2] - vLocalMin[2]);
}

void wlVolumeGetVolumeWorldMid(const WorldVolume *volume, Vec3 world_mid)
{
	if (world_mid && eaSize(&volume->octree_entries))
		copyVec3(volume->octree_entries[0]->bounds.mid, world_mid);
}

void wlVolumeGetWorldMinMax(const WorldVolume *volume, Vec3 world_min, Vec3 world_max)
{
	if (world_min && eaSize(&volume->octree_entries))
		copyVec3(volume->octree_entries[0]->bounds.min, world_min);
	if (world_max && eaSize(&volume->octree_entries))
		copyVec3(volume->octree_entries[0]->bounds.max, world_max);
}

// Assumes just one element in volume
void wlVolumeGetWorldPosRotMinMax(SA_PARAM_NN_VALID const WorldVolume *volume, Vec3 world_pos, Quat rot, Vec3 local_min, Vec3 local_max)
{
	WorldVolumeElement *element = volume->elements[0];
	if (element && element->volume_shape == WL_VOLUME_SPHERE) 
	{
		copyVec3(element->world_mat[3], world_pos);
		if (local_min)
		{
			local_min[0] = world_pos[0] - element->radius;
			local_min[1] = world_pos[1] - element->radius;
			local_min[2] = world_pos[2] - element->radius;
		}
		if (local_max)
		{
			local_max[0] = world_pos[0] + element->radius;
			local_max[1] = world_pos[1] + element->radius;
			local_max[2] = world_pos[2] + element->radius;
		}
		if (rot)
			zeroQuat(rot);
	} 
	else if (element && element->volume_shape == WL_VOLUME_BOX) 
	{
		copyVec3(element->world_mat[3], world_pos);
		if (local_min)
			copyVec3(element->local_min, local_min);
		if (local_max)
			copyVec3(element->local_max, local_max);
		if (rot)
			mat3ToQuat(element->world_mat, rot);
	} 
	else 
	{
		assertmsg(0, "Unsupported volume shape for this function");
	}
}

WorldVolumeQueryCache **wlVolumeGetCachedQueries(const WorldVolume *volume)
{
	return volume->caches;
}

void wlVolumeSetQueryCallbacks(WorldVolume *volume, WorldVolumeQueryCacheChangeCallback entered_callback, WorldVolumeQueryCacheChangeCallback exited_callback, WorldVolumeQueryCacheChangeCallback remain_callback)
{
	volume->query_entered_callback = entered_callback;
	volume->query_exited_callback = exited_callback;
	volume->remain_inside_callback = remain_callback;
}

void wlVolumeSetDataFreeCallback(SA_PARAM_NN_VALID WorldVolume *volume, WorldVolumeDataFreeCallback free_callback)
{
	volume->data_free_callback = free_callback;
}

WorldVolumeQueryCache *wlVolumeQueryCacheCreate_dbg(int iPartitionIdx, U32 query_type, void *query_data MEM_DBG_PARMS)
{
	WorldVolumeQueryCache *query_cache = scalloc(1, sizeof(WorldVolumeQueryCache));
	MEM_DBG_STRUCT_PARMS_INIT(query_cache);
	query_cache->iPartitionIdx = iPartitionIdx;
	query_cache->query_type = query_type;
	query_cache->query_data = query_data;
	return query_cache;
}

void wlVolumeQueryCacheFree(WorldVolumeQueryCache *query_cache)
{
	int i;

 	if (!query_cache)
		return;

	PERFINFO_AUTO_START_FUNC();

	// remove the cache from the volumes it is in
	for (i = 0; i < eaSize(&query_cache->volumes); ++i)
		wlVolumeRemoveQueryCache(query_cache->volumes[i], query_cache);

	eaDestroy(&query_cache->prev_volumes);
	eaDestroy(&query_cache->volumes);

	free(query_cache);

	PERFINFO_AUTO_STOP();
}

void *wlVolumeQueryCacheGetData(const WorldVolumeQueryCache *query_cache)
{
	return query_cache->query_data;
}

U32 wlVolumeQueryCacheGetType(const WorldVolumeQueryCache *query_cache)
{
	return query_cache->query_type;
}

bool wlVolumeQueryCacheIsType(const WorldVolumeQueryCache *query_cache, U32 query_type)
{
	return (query_cache->query_type & query_type) == query_type;
}

void wlVolumeQuerySetCallbacks(WorldVolumeQueryCache *query_cache, WorldVolumeQueryCacheChangeCallback entered_callback, WorldVolumeQueryCacheChangeCallback exited_callback, WorldVolumeQueryCacheChangeCallback remain_callback)
{
	query_cache->query_entered_callback = entered_callback;
	query_cache->query_exited_callback = exited_callback;
	query_cache->remain_inside_callback = remain_callback;
}

static bool checkVolumeElementInQuery(WorldVolumeElement *element, WorldVolumeQueryCache *query_cache)
{
	bool bResult = false;

	PERFINFO_AUTO_START_FUNC();

	switch (query_cache->last_query_type)
	{
		xcase WL_QUERY_BOX:
		{
			switch (element->volume_shape)
			{
				xcase WL_VOLUME_HULL:
					bResult = hullBoxCollision(element->hull, element->mesh, 
											query_cache->local_min, query_cache->local_max, 
											query_cache->world_mat, query_cache->inv_world_mat);

				xcase WL_VOLUME_BOX:
					bResult = orientBoxBoxCollision(query_cache->local_min, query_cache->local_max, 
													query_cache->world_mat,
													element->local_min, element->local_max, 
													element->world_mat);

				xcase WL_VOLUME_SPHERE:
					bResult = sphereOrientBoxCollision(element->world_mat[3], element->radius,
													query_cache->local_min, query_cache->local_max, 
													query_cache->world_mat, query_cache->inv_world_mat);

				xdefault:
					devassert(0);
			}
		}

		xcase WL_QUERY_SPHERE:
		{
			switch (element->volume_shape)
			{
				xcase WL_VOLUME_HULL:
					bResult = hullIsSphereInside(element->hull, query_cache->world_mid, query_cache->radius);

				xcase WL_VOLUME_BOX:
					bResult = sphereOrientBoxCollision(	query_cache->world_mid, query_cache->radius,
														element->local_min, element->local_max, 
														element->world_mat, element->inv_world_mat);

				xcase WL_VOLUME_SPHERE:
					bResult = sphereSphereCollision(query_cache->world_mid, query_cache->radius, 
													element->world_mat[3], element->radius);

				xdefault:
					devassert(0);
			}
		}

		xcase WL_QUERY_CAPSULE:
		{
			switch (element->volume_shape)
			{
				xcase WL_VOLUME_HULL:
					bResult = hullCapsuleCollision(element->hull, query_cache->cap_start, query_cache->cap_dir, 
												query_cache->length, query_cache->radius, query_cache->world_mat);

				xcase WL_VOLUME_BOX:
					bResult = capsuleBoxCollision(query_cache->cap_start, query_cache->cap_dir, 
												query_cache->length, query_cache->radius, query_cache->world_mat, 
												element->local_min, element->local_max, 
												element->world_mat, element->inv_world_mat);

				xcase WL_VOLUME_SPHERE:
					bResult = capsuleSphereCollision(query_cache->cap_start, query_cache->cap_dir, 
													query_cache->length, query_cache->radius, query_cache->world_mat, 
													element->world_mat[3], element->radius);

				xdefault:
					devassert(0);
			}
		}
	}

	PERFINFO_AUTO_STOP();
	return bResult;;
}

static int checkVolumeInQuery(void *node, int node_type, const Vec3 scenter, F32 sradius, void *user_data)
{
	WorldVolumeQueryCache *query_cache = user_data;
	WorldVolume *volume = node;
	int i;

	PERFINFO_AUTO_START_FUNC();

	// determine if query volume overlaps eny element of the node volume
	for (i = 0; i < eaSize(&volume->elements); ++i)
	{
		if (checkVolumeElementInQuery(volume->elements[i], query_cache))
		{
			PERFINFO_AUTO_STOP();
			return 1;
		}
	}

	PERFINFO_AUTO_STOP();
	return 0;
}

static void wlVolumeCacheUpdate(WorldVolumeQueryCache *query_cache)
{
	int i;

	PERFINFO_AUTO_START_FUNC();

	for (i = 0; i < eaSize(&query_cache->volumes); ++i)
	{
		int idx = eaFind(&query_cache->prev_volumes, query_cache->volumes[i]);
		if (idx < 0)
		{
			// new volume
			wlVolumeAddQueryCache(query_cache->volumes[i], query_cache);
		}
		else
		{
			// still in volume
			eaRemoveFast(&query_cache->prev_volumes, idx);
		}
	}

	// all that's left in the previous volume list are those that the query is no longer in
	for (i = 0; i < eaSize(&query_cache->prev_volumes); ++i)
	{
		wlVolumeRemoveQueryCache(query_cache->prev_volumes[i], query_cache);
	}

	steaSetSize(&query_cache->prev_volumes, 0, query_cache);

	PERFINFO_AUTO_STOP();
}

static void wlVolumeCacheTriggerRemainsCallbacks(WorldVolumeQueryCache *query_cache)
{
	int i, iNumVolumes;

	PERFINFO_AUTO_START_FUNC();

	iNumVolumes = eaSize(&query_cache->volumes);
	for (i = 0; i < iNumVolumes; ++i)
	{
		WorldVolume* volume = query_cache->volumes[i];
		if (volume->remain_inside_callback)
			volume->remain_inside_callback(volume, query_cache);
		if (query_cache->remain_inside_callback)
			query_cache->remain_inside_callback(volume, query_cache);
	}

	PERFINFO_AUTO_STOP();
}

static int cmpVolumes(const WorldVolume** a, const WorldVolume** b)
{
	F32 diff = (*a)->volume_size - (*b)->volume_size;
	if (diff == 0)
		return 0;
	return SIGN(diff);
}

const WorldVolume **wlVolumeCacheQueryBox(WorldVolumeQueryCache *query_cache, const Mat4 world_mat, const Vec3 local_min, const Vec3 local_max)
{
	return wlVolumeCacheQueryBoxByType(query_cache, world_mat, local_min, local_max, all_volume_types);
}

#define ROT_TOL 0.2f
#define POS_TOL 0.5f

const WorldVolume **wlVolumeCacheQueryBoxByType(WorldVolumeQueryCache *query_cache, const Mat4 world_mat, const Vec3 local_min, const Vec3 local_max, U32 volume_type)
{
	WorldVolumePartitionData *pData;
	bool need_query;
	U32 volume_type_bit;

	pData = wlVolumeGetPartitionData(query_cache->iPartitionIdx);

	if (!pData->eaVolumeOctrees)
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("Update need_query", 1);

	if (volume_type == 0xffffffff || !volume_type)
		volume_type = all_volume_types;

	need_query = query_cache->last_query_mod_time != pData->uVolumeModTime;
	need_query = need_query || query_cache->last_query_type!=WL_QUERY_BOX;
	need_query = need_query || (query_cache->query_volume_type!=volume_type);
	need_query = need_query || !nearSameMat4Tol(world_mat, query_cache->world_mat, ROT_TOL, POS_TOL);
	need_query = need_query || !nearSameVec3Tol(local_min, query_cache->local_min, POS_TOL);
	need_query = need_query || !nearSameVec3Tol(local_max, query_cache->local_max, POS_TOL);

	PERFINFO_AUTO_STOP();
	
	if (need_query)
	{
		Vec3 world_mid, local_mid;
		F32 radius;
		WorldVolume **temp_volumes;
		int i;

		PERFINFO_AUTO_START("Set up query", 1);

		query_cache->query_volume_type = volume_type;
		query_cache->last_query_mod_time = pData->uVolumeModTime;
		query_cache->last_query_type = WL_QUERY_BOX;
		copyMat4(world_mat, query_cache->world_mat);
		invertMat4Copy(world_mat, query_cache->inv_world_mat);
		copyVec3(local_min, query_cache->local_min);
		copyVec3(local_max, query_cache->local_max);

		centerVec3(local_min, local_max, local_mid);
		mulVecMat4(local_mid, world_mat, world_mid);
		radius = distance3(local_min, local_max) * 0.5f;

		temp_volumes = query_cache->prev_volumes;
		query_cache->prev_volumes = query_cache->volumes;
		query_cache->volumes = temp_volumes;

		PERFINFO_AUTO_STOP();

		// Find volumes
		assert(!eaSize(&query_cache->volumes));
		if (wlIsServer() && volume_type == all_volume_types)
		{
			if (pData->pAllTypeVolumeOctree)
				octreeFindInSphereEA_dbg(pData->pAllTypeVolumeOctree, &query_cache->volumes, world_mid, radius, checkVolumeInQuery, query_cache MEM_DBG_STRUCT_PARMS_CALL(query_cache));
		}
		else
		{
			for (volume_type_bit = 1; volume_type_bit <= volume_type; volume_type_bit <<= 1)
			{
				if (volume_type & volume_type_bit)
				{
					Octree *volume_octree;

					PERFINFO_AUTO_START("Check volume", 1);

					volume_octree = getOctree(query_cache->iPartitionIdx, volume_type_bit, false);
					if (volume_octree)
						octreeFindInSphereEA_dbg(volume_octree, &query_cache->volumes, world_mid, radius, checkVolumeInQuery, query_cache MEM_DBG_STRUCT_PARMS_CALL(query_cache));

					PERFINFO_AUTO_STOP();
				}
			}
		}

		// Sort and remove duplicate entries
		PERFINFO_AUTO_START("Sort volumes", 1);
		eaQSortG(query_cache->volumes, cmpVolumes);
		for (i = eaSize(&query_cache->volumes)-1; i >= 1; --i)
		{
			if (query_cache->volumes[i] == query_cache->volumes[i-1])
				eaRemove(&query_cache->volumes, i);
		}
		PERFINFO_AUTO_STOP();

		// Perform updates
		wlVolumeCacheUpdate(query_cache);
	}

	wlVolumeCacheTriggerRemainsCallbacks(query_cache);

	PERFINFO_AUTO_STOP();

	return query_cache->volumes;
}

const WorldVolume **wlVolumeCacheQuerySphere(WorldVolumeQueryCache *query_cache, const Vec3 world_mid, F32 radius)
{
	return wlVolumeCacheQuerySphereByType(query_cache, world_mid, radius, all_volume_types);
}

const WorldVolume **wlVolumeCacheQuerySphereByType(WorldVolumeQueryCache *query_cache, const Vec3 world_mid, F32 radius, U32 volume_type)
{
	WorldVolumePartitionData *pData;
	bool need_query;
	U32 volume_type_bit;

	pData = wlVolumeGetPartitionData(query_cache->iPartitionIdx);

	if (!pData->eaVolumeOctrees)
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	if (volume_type == 0xffffffff || !volume_type)
		volume_type = all_volume_types;

	need_query = query_cache->last_query_mod_time != pData->uVolumeModTime;
	need_query = need_query || query_cache->last_query_type!=WL_VOLUME_SPHERE;
	need_query = need_query || !nearSameVec3Tol(world_mid, query_cache->world_mid, POS_TOL);
	need_query = need_query || !nearSameF32(radius, query_cache->radius);
	need_query = need_query || (query_cache->query_volume_type!=volume_type);

	if (need_query)
	{
		WorldVolume **temp_volumes;
		int i;

		query_cache->query_volume_type = volume_type;
		query_cache->last_query_mod_time = pData->uVolumeModTime;
		query_cache->last_query_type = WL_QUERY_SPHERE;
		copyVec3(world_mid, query_cache->world_mid);
		query_cache->radius = radius;

		temp_volumes = query_cache->prev_volumes;
		query_cache->prev_volumes = query_cache->volumes;
		query_cache->volumes = temp_volumes;

		// Find volumes
		assert(!eaSize(&query_cache->volumes));
		if (wlIsServer() && volume_type == all_volume_types)
		{
			if (pData->pAllTypeVolumeOctree)
				octreeFindInSphereEA_dbg(pData->pAllTypeVolumeOctree, &query_cache->volumes, world_mid, radius, checkVolumeInQuery, query_cache MEM_DBG_STRUCT_PARMS_CALL(query_cache));
		}
		else
		{
			for (volume_type_bit = 1; volume_type_bit <= volume_type; volume_type_bit <<= 1)
			{
				if (volume_type & volume_type_bit)
				{
					Octree *volume_octree = getOctree(query_cache->iPartitionIdx, volume_type_bit, false);
					if (volume_octree)
						octreeFindInSphereEA_dbg(volume_octree, &query_cache->volumes, world_mid, radius, checkVolumeInQuery, query_cache MEM_DBG_STRUCT_PARMS_CALL(query_cache));
				}
			}
		}

		// Sort and remove duplicate entries
		eaQSortG(query_cache->volumes, cmpVolumes);
		for (i = eaSize(&query_cache->volumes)-1; i >= 1; --i)
		{
			if (query_cache->volumes[i] == query_cache->volumes[i-1])
				eaRemove(&query_cache->volumes, i);
		}

		// Perform updates
		wlVolumeCacheUpdate(query_cache);
	}

	PERFINFO_AUTO_STOP();

	return query_cache->volumes;
}

const WorldVolume **wlVolumeCacheQueryCapsule(WorldVolumeQueryCache *query_cache, const Capsule *cap, const Mat4 world_mat)
{
	return wlVolumeCacheQueryCapsuleByType(query_cache, cap, world_mat, all_volume_types);
}

const WorldVolume **wlVolumeCacheQueryCapsuleByType(WorldVolumeQueryCache *query_cache, const Capsule *cap, const Mat4 world_mat, U32 volume_type)
{
	bool need_query;
	U32 volume_type_bit;
	WorldVolumePartitionData *pData;

	pData = wlVolumeGetPartitionData(query_cache->iPartitionIdx);

	if (!pData->eaVolumeOctrees)
		return NULL;

	PERFINFO_AUTO_START_FUNC();

	PERFINFO_AUTO_START("QueryNeededTest",1 );

	if (volume_type == 0xffffffff || !volume_type)
		volume_type = all_volume_types;

	need_query = (query_cache->last_query_mod_time != pData->uVolumeModTime)
				 || (query_cache->last_query_type != WL_QUERY_CAPSULE)
				 || (query_cache->query_volume_type != volume_type)
				 || !nearSameMat4Tol(world_mat, query_cache->world_mat, ROT_TOL, POS_TOL)
				 || !nearSameVec3Tol(cap->vStart, query_cache->cap_start, POS_TOL)
				 || !nearSameVec3Tol(cap->vDir, query_cache->cap_dir, POS_TOL)
				 || !nearSameF32(cap->fRadius, query_cache->radius)
				 || !nearSameF32(cap->fLength, query_cache->length);

	PERFINFO_AUTO_STOP(); // QueryNeededTest

	if (need_query)
	{
		Vec3 world_mid, temp;
		F32 world_radius;
		WorldVolume **temp_volumes;
		int i;

		PERFINFO_AUTO_START("Setup", 1);

		query_cache->query_volume_type = volume_type;
		query_cache->last_query_mod_time = pData->uVolumeModTime;
		query_cache->last_query_type = WL_QUERY_CAPSULE;
		copyMat4(world_mat, query_cache->world_mat);
		copyVec3(cap->vStart, query_cache->cap_start);
		copyVec3(cap->vDir, query_cache->cap_dir);
		query_cache->radius = cap->fRadius;
		query_cache->length = cap->fLength;

		temp_volumes = query_cache->prev_volumes;
		query_cache->prev_volumes = query_cache->volumes;
		query_cache->volumes = temp_volumes;

		scaleAddVec3(query_cache->cap_dir, cap->fLength/2, query_cache->cap_start, temp);
		mulVecMat4(temp, world_mat, world_mid);
		world_radius = query_cache->length/2 + query_cache->radius;

		PERFINFO_AUTO_STOP(); // Setup

		// Find volumes
		assert(!eaSize(&query_cache->volumes));
		if (wlIsServer() && volume_type == all_volume_types)
		{
			PERFINFO_AUTO_START("QueryingAllVolumes", 1);

			if (pData->pAllTypeVolumeOctree)
				octreeFindInSphereEA_dbg(pData->pAllTypeVolumeOctree, &query_cache->volumes, world_mid, world_radius, checkVolumeInQuery, query_cache MEM_DBG_STRUCT_PARMS_CALL(query_cache));

			PERFINFO_AUTO_STOP(); // QueryingAllVolumes
		}
		else
		{
			PERFINFO_AUTO_START("QueryingSomeVolumes", 1);

			for (volume_type_bit = 1; volume_type_bit <= volume_type; volume_type_bit <<= 1)
			{
				if (volume_type & volume_type_bit)
				{
					Octree *volume_octree = getOctree(query_cache->iPartitionIdx, volume_type_bit, false);
					if (volume_octree)
						octreeFindInSphereEA_dbg(volume_octree, &query_cache->volumes, world_mid, world_radius, checkVolumeInQuery, query_cache MEM_DBG_STRUCT_PARMS_CALL(query_cache));
				}
			}

			PERFINFO_AUTO_STOP(); // QueryingSomeVolumes
		}

		PERFINFO_AUTO_START("Sorting",1);

		// Sort and remove duplicate entries
		eaQSortG(query_cache->volumes, cmpVolumes);
		for (i = eaSize(&query_cache->volumes)-1; i >= 1; --i)
		{
			if (query_cache->volumes[i] == query_cache->volumes[i-1])
				eaRemove(&query_cache->volumes, i);
		}

		PERFINFO_AUTO_STOP(); // Sorting

		// Perform updates
		wlVolumeCacheUpdate(query_cache);
	}

	wlVolumeCacheTriggerRemainsCallbacks(query_cache);

	PERFINFO_AUTO_STOP();

	return query_cache->volumes;
}

const WorldVolume **wlVolumeCacheGetCachedVolumes(WorldVolumeQueryCache *query_cache)
{
	WorldVolumePartitionData *pData = wlVolumeGetPartitionData(query_cache->iPartitionIdx);

	if (query_cache->last_query_mod_time != pData->uVolumeModTime)
		return NULL;
	return query_cache->volumes;
}

//////////////////////////////////////////////////////////////////////////

static int last_raycollide_time;

static int checkVolumeIsCollidable(void *node, int node_type, const Vec3 scenter, F32 sradius, void *user_data)
{
	WorldVolume *volume = node;

	// see if it has already been tested
	if (volume->last_raycollide_time == last_raycollide_time)
		return 0;

	return 1;
}

#define MARCHING_SIZE 50.f
bool wlVolumeRayCollide(int iPartitionIdx, const Vec3 start, const Vec3 end, U32 volume_type, Vec3 hit_location)
{
	int i, j, num_steps;
	Vec3 step_size, current_start;
	WorldVolume **volumes = NULL;
	Octree *volume_octree = getOctree(iPartitionIdx, volume_type, false);

	if (!volume_octree)
		return false;

	PERFINFO_AUTO_START_FUNC();

	{
		Vec3 boxTestMin;
		Vec3 boxTestMax;

		// Box collision needs min to actually be less than max, so in
		// case a ray is pointing backwards on any axis, we need to flip
		// that around.
		for(i = 0; i < 3; i++) {
			boxTestMin[i] = start[i] < end[i] ? start[i] : end[i];
			boxTestMax[i] = start[i] > end[i] ? start[i] : end[i];
		}

		if (!octreeTestBox(volume_octree, boxTestMin, boxTestMax))
		{
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	++last_raycollide_time;

	subVec3(end, start, step_size);
	num_steps = ceil(normalVec3(step_size) / MARCHING_SIZE);
	scaleVec3(step_size, MARCHING_SIZE, step_size);

	copyVec3(start, current_start);
	for (i = 0; i < num_steps; ++i)
	{
		Vec3 current_end, current_mid;
		F32 current_step_size;
		bool has_hit = false;

		if (i == num_steps-1)
		{
			copyVec3(end, current_end);
			centerVec3(current_start, current_end, current_mid);
			current_step_size = distance3(current_end, current_start);
		}
		else
		{
			addVec3(current_start, step_size, current_end);
			scaleAddVec3(step_size, 0.5f, current_start, current_mid);
			current_step_size = MARCHING_SIZE;
		}

		eaSetSize(&volumes, 0);
		octreeFindInSphereEA(volume_octree, &volumes, current_mid, current_step_size * 0.5f, checkVolumeIsCollidable, &volume_type);

		for (j = 0; j < eaSize(&volumes); ++j)
			volumes[j]->last_raycollide_time = last_raycollide_time;

		has_hit = wlVolumeRayCollideSpecifyVolumes(start, end, volumes, hit_location);

		if (has_hit)
		{
			eaDestroy(&volumes);
			PERFINFO_AUTO_STOP();
			return true;
		}

		copyVec3(current_end, current_start);
	}
	
	eaDestroy(&volumes);
	PERFINFO_AUTO_STOP();
	return false;
}

static bool wlVolumeElementRayCollide(const Vec3 start, const Vec3 end, const WorldVolumeElement *element, bool has_hit, Vec3 hit_location)
{
	switch (element->volume_shape)
	{
		xcase WL_VOLUME_BOX:
		{
			Vec3 local_start, local_end, local_hit;
			mulVecMat4(start, element->inv_world_mat, local_start);
			mulVecMat4(end, element->inv_world_mat, local_end);
			if (lineBoxCollisionHollow(local_start, local_end, element->local_min, element->local_max, local_hit, element->face_bits))
			{
				if (hit_location)
				{
					Vec3 world_hit;
					mulVecMat4(local_hit, element->world_mat, world_hit);
					if (!has_hit || distance3Squared(world_hit, start) < distance3Squared(hit_location, start))
					{
						copyVec3(world_hit, hit_location);
						return true;
					}
				}
				else
				{
					return true;
				}
			}
		}

		xcase WL_VOLUME_SPHERE:
		{
			Vec3 world_hit;
			if (sphereLineCollisionWithHitPoint(start, end, element->world_mat[3], element->radius, world_hit))
			{
				if (hit_location)
				{
					if (!has_hit || distance3Squared(world_hit, start) < distance3Squared(hit_location, start))
					{
						copyVec3(world_hit, hit_location);
						return true;
					}
				}
				else
				{
					return true;
				}
			}
		}

		xcase WL_VOLUME_HULL:
		{
			// TODO (jdejesus)
			// not sure if this will ever be used with hulls
		}
	}

	return false;
}

bool wlVolumeRayCollideSpecifyVolumes(const Vec3 start, const Vec3 end, const WorldVolume **volumes, Vec3 hit_location)
{
	bool has_hit = false;
	int i, j;

	for (j = 0; j < eaSize(&volumes); ++j)
	{
		const WorldVolume *volume = volumes[j];

		for (i = 0; i < eaSize(&volume->elements); ++i)
		{
			if (wlVolumeElementRayCollide(start, end, volume->elements[i], has_hit, hit_location))
			{
				has_hit = true;
				if (!hit_location)
					return true;
			}
		}
	}

	return has_hit;
}

AUTO_RUN;
void wlVolume_InitSystem(void)
{
	// Initialize the query volume type
	s_EntityVolumeQueryType = wlVolumeQueryCacheTypeNameToBitMask("Entity");
}

U32 wlVolumeGetWorldVolumeStateTimestamp(int iPartitionIdx)
{
	WorldVolumePartitionData *pData = wlVolumeGetPartitionData(iPartitionIdx);
	return pData->uVolumeModTime;
}

#include "wlVolumes_h_ast.c"
