#ifndef NO_EDITORS

#define GENESIS_ALLOW_OLD_HEADERS 1

#include "Genesis.h"

#include"EditorServerMain.h"
#include"GenesisMissions.h"
#include"SharedMemory.h"
#include"StringCache.h"
#include"StringUtil.h"
#include"WorldGrid.h"
#include"WorldLib.h"
#include"RegionRules.h"
#include"error.h"
#include "Entity.h"
#include"net.h"
#include "oldencounter_common.h"
#include"rand.h"
#include"wlGenesis.h"
#include"wlGenesisMissions.h"
#include"wlGenesisSolarSystem.h"
#include"wlGenesisInterior.h"
#include"wlGenesisExterior.h"
#include"wlGenesisExteriorNode.h"
#include"wlGenesisExteriorDesign.h"
#include"wlGenesisRoom.h"
#include"wlUGC.h"
#include"textparser.h"
#include"fileutil2.h"
#ifdef GAMECLIENT
#include"WorldEditorClientMain.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Editors););

static void genesisWriteAllMissions(ZoneMapInfo* zmap_info);

void genesisMakeRoomNamesList(GenesisMapDescription *pMapDesc, char ***pppNameList)
{
	int layoutIt;
	for ( layoutIt=0; layoutIt < eaSize(&pMapDesc->solar_system_layouts); layoutIt++ ) {
		int pointListIt;
		int pointIt;
		GenesisShoeboxLayout* shoebox = &pMapDesc->solar_system_layouts[layoutIt]->shoebox;
		for( pointListIt = 0; pointListIt != eaSize( &shoebox->point_lists ); ++pointListIt ) {
			ShoeboxPointList* pointList = shoebox->point_lists[ pointListIt ];
			for( pointIt = 0; pointIt != eaSize( &pointList->points ); ++pointIt ) {
				eaPush( pppNameList, strdup( pointList->points[ pointIt ]->name ));
			}

			if( pointList->list_type == SBLT_Orbit && pointList->name && pointList->name[ 0 ]) {
				eaPush( pppNameList, strdup( pointList->name ));
			}
		}
	}
	for ( layoutIt=0; layoutIt < eaSize(&pMapDesc->interior_layouts); layoutIt++ ) {
		int roomIt;
		GenesisInteriorLayout *pLayout = pMapDesc->interior_layouts[layoutIt];
		for( roomIt = 0; roomIt != eaSize( &pLayout->rooms ); ++roomIt ) {
			eaPush( pppNameList, strdup( pLayout->rooms[ roomIt ]->name ));
		}
	}
	if( pMapDesc->exterior_layout ) {
		int roomIt;
		for( roomIt = 0; roomIt != eaSize( &pMapDesc->exterior_layout->rooms ); ++roomIt ) {
			eaPush( pppNameList, strdup( pMapDesc->exterior_layout->rooms[ roomIt ]->name ));
		}
	}	
}

bool genesisTransmogrifyMapDesc(ZoneMapInfo *zminfo, GenesisRuntimeStatus *gen_status, const char* map_prefix)
{
	GenesisZoneMapData *data = zmapInfoGetGenesisData(zminfo);
	GenesisMapDescription *map_desc = SAFE_MEMBER(data, map_desc);
	int i, j;

	if(	!data || !map_desc)
		return false;

	if (map_prefix) {
		genesisSetStageAndAdd(gen_status, "%s: Transmogrifier", map_prefix);
	} else {
		genesisSetStageAndAdd(gen_status, "Transmogrifier");
	}
	
	{
		GenesisMissionZoneChallenge **zone_challenges = NULL;
		GenesisZoneMission **zone_missions = NULL;
		GenesisProceduralEncounterProperties **encounter_properties = NULL;
		for(i = 0; i != eaSize(&map_desc->missions); ++i)
		{
			GenesisZoneMission* zone_mission;
			
			zone_mission = genesisTransmogrifyMission(zminfo, map_desc, i );
			if( zone_mission ) {
				eaPush( &zone_missions, zone_mission );
			}
			for(j = 0; j != eaSize(&map_desc->missions[i]->eaChallenges); ++j)
			{
				genesisTransmogrifyChallengePEPHack( map_desc, i, map_desc->missions[i]->eaChallenges[j], &encounter_properties);
			}
		}
		
		zone_challenges = genesisTransmogrifySharedChallenges(zminfo, map_desc);
		for( i = 0; i != eaSize( &map_desc->shared_challenges ); ++i ) {
			genesisTransmogrifyChallengePEPHack( map_desc, -1, map_desc->shared_challenges[ i ], &encounter_properties );
		}

		zmapInfoSetGenesisZoneMissions(zminfo, zone_missions);
		zmapInfoSetSharedGenesisZoneChallenges(zminfo, zone_challenges);
		zmapInfoSetEncounterOverrides(zminfo, encounter_properties);
	}

	data->is_map_tracking_enabled = map_desc->is_tracking_enabled;
	
	eaDestroyStruct(&data->solar_systems, parse_GenesisSolSysZoneMap);
	for ( i=0; i < eaSize(&map_desc->solar_system_layouts); i++ )
	{
		GenesisSolSysZoneMap *solar_system = StructCreate(parse_GenesisSolSysZoneMap);
		eaPush(&data->solar_systems, solar_system);
		genesisTransmogrifySolarSystem(data->seed, data->detail_seed, map_desc, map_desc->solar_system_layouts[i], solar_system);
	}
	eaDestroyStruct(&data->genesis_interiors, parse_GenesisZoneInterior);
	for ( i=0; i < eaSize(&map_desc->interior_layouts); i++ )
	{
		GenesisZoneInterior *interior = StructCreate(parse_GenesisZoneInterior);
		eaPush(&data->genesis_interiors, interior);
		genesisTransmogrifyInterior(data->seed, data->detail_seed, map_desc, map_desc->interior_layouts[i], interior);
	}
	if (map_desc->exterior_layout)
	{
		if(data->genesis_exterior)
			StructDestroy(parse_GenesisZoneExterior, data->genesis_exterior);
		data->genesis_exterior = StructCreate(parse_GenesisZoneExterior);

		if(data->genesis_exterior_nodes)
		{
			StructDestroy(parse_GenesisZoneNodeLayout, data->genesis_exterior_nodes);
			data->genesis_exterior_nodes = StructCreate(parse_GenesisZoneNodeLayout);
		}
		genesisTransmogrifyExterior(data->seed, data->detail_seed, map_desc, map_desc->exterior_layout, data->genesis_exterior);
	}

	genesisSetStage(NULL);

	// Detect failure
	if (genesisStatusFailed(gen_status))
		return false;
	return true;
}

static void genesisSetNewLayoutSeeds(GenesisZoneMapData* data)
{
	int j;

	if(!data->map_desc)
		return;

	for ( j=0; j < eaSize(&data->map_desc->interior_layouts); j++ )
	{
		GenesisInteriorLayout *layout = data->map_desc->interior_layouts[j];
		layout->common_data.layout_seed = 0;
	}
	for ( j=0; j < eaSize(&data->map_desc->solar_system_layouts); j++ )
	{
		GenesisSolSysLayout *layout = data->map_desc->solar_system_layouts[j];
		layout->common_data.layout_seed = 0;
	}
	if(data->map_desc->exterior_layout)
	{
		GenesisExteriorLayout *layout = data->map_desc->exterior_layout;
		layout->common_data.layout_seed = 0;
	}
}

static void genesisSetNewDetailSeeds(GenesisZoneMapData* data)
{
	int i, j;
	
	data->detail_seed = randomU32();
	
	if(!data->map_desc)
		return;

	for ( j=0; j < eaSize(&data->map_desc->interior_layouts); j++ )
	{
		GenesisInteriorLayout *layout = data->map_desc->interior_layouts[j];
		for ( i=0; i < eaSize(&layout->rooms); i++ )
			layout->rooms[i]->detail_seed = 0;
		for ( i=0; i < eaSize(&layout->paths); i++ )
			layout->paths[i]->detail_seed = 0;
	}
	if(data->map_desc->exterior_layout)
	{
		GenesisExteriorLayout *layout = data->map_desc->exterior_layout;
		for ( i=0; i < eaSize(&layout->rooms); i++ )
			layout->rooms[i]->detail_seed = 0;
		for ( i=0; i < eaSize(&layout->paths); i++ )
			layout->paths[i]->detail_seed = 0;
	}
}

GenesisRuntimeStatus *genesisReseedMapDesc(int iPartitionIdx, ZoneMap *zmap, bool seed_layout, bool seed_detail, const char* external_map_name)
{
	GenesisZoneMapData* data = zmapInfoGetGenesisData(zmapGetInfo(zmap));
	GenesisRuntimeStatus *gen_status;
	GenesisMapDescription *map_desc = SAFE_MEMBER(data, map_desc);

	gen_status = StructCreate(parse_GenesisRuntimeStatus);

	if (!data)
		return gen_status;

	if (data->skip_terrain_update && !seed_layout && !seed_detail && zmapGetLayerCount(zmap) == 1)
	{
		ZoneMapLayer *terrain_layer = zmapGetLayer(zmap, 0);
		GenesisZoneMapData *proc_data = zmapInfoGetGenesisData(zmapGetInfo(zmap));
		GenesisMissionRequirements **mission_reqs = NULL;
		GroupDef *root_def;
		int i;

		if(!genesisTransmogrifyMapDesc(zmapGetInfo(zmap), gen_status, external_map_name))
			return gen_status;

		trackerClose(layerGetTracker(terrain_layer));
		root_def = layerGetDef(terrain_layer);

		proc_data = zmapInfoGetGenesisData(zmapGetInfo(zmap));

		for (i = 0; proc_data && i != eaSize(&proc_data->genesis_mission); ++i)
		{
			GenesisMissionRequirements *req;
			req = genesisGenerateMission(zmapGetInfo(zmap), i, proc_data->genesis_mission[ i ], NULL, false, "Genesis", false);
			if( !req ) {
				eaDestroyStruct( &mission_reqs, parse_GenesisMissionRequirements );
				return gen_status;
			}
			eaPush( &mission_reqs, req );
		}

		eaClearStruct(&root_def->children, parse_GroupChild);
		stashTableClear(root_def->name_to_path);
		stashTableClear(root_def->path_to_name);

		genesisResetLayout();
		if(external_map_name) {
			genesisSetStageAndAdd(gen_status, "%s: Exterior Layout", external_map_name);
		} else {
			genesisSetStageAndAdd(gen_status, "Exterior Layout");
		}
		genesisExteriorPopulateLayer(iPartitionIdx, zmap, mission_reqs, terrain_layer);

		eaDestroyStruct( &mission_reqs, parse_GenesisMissionRequirements );

		if( !external_map_name ) {
			worldUpdateBounds(true, false);
		}
		zmapTrackerUpdate(zmap, false, true);

		return gen_status;
	}

	if (seed_layout) {
		data->seed = randomU32();
		genesisSetNewLayoutSeeds(data);
	}
	if (seed_detail)
		genesisSetNewDetailSeeds(data);
	if(!genesisTransmogrifyMapDesc(zmapGetInfo(zmap), gen_status, external_map_name))
		return gen_status;

	//Update Zmap
	if( !external_map_name) {
		worldUpdateBounds(true, false);
	}
	while (zmapGetLayerCount(zmap) > 0)
		zmapRemoveLayer(zmap, 0);
	if( !external_map_name ) {
		worldUpdateBounds(true, false);
	}
	zmapTrackerUpdate(zmap, false, true);

	//Make Layers
	genesisFixupZoneMapInfo(zmapGetInfo(zmap));
	genesisMakeLayers(zmap);

	printf("Reseeding with seed: %d\n", data->seed);

	zmapInfoSetModified(zmapGetInfo(zmap));

	if(external_map_name) {
		genesisSetStageAndAdd(gen_status, "%s: %s", external_map_name, "Generate");
	} else {
		genesisSetStageAndAdd(gen_status, "%s", "Generate");
	}

	genesisRebuildLayers(iPartitionIdx, zmap, external_map_name != NULL);
	
	genesisSetStage(NULL);

	return gen_status;
}

void genesisReseedExternalMapDescs(int iPartitionIdx, GenesisRuntimeStatus* genStatus, ZoneMapInfo** zmapInfos, bool seed_layout, bool seed_detail)
{
	int it;
	for( it = 0; it != eaSize( &zmapInfos ); ++it ) {
		ZoneMapInfo* zmapInfo = zmapInfos[ it ];

		if( !zmapInfoLocked( zmapInfo )) {
			genesisSetStageAndAdd( genStatus, "%s: Load/Save", zmapInfoGetPublicName( zmapInfo ));
			genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapInfo )), "Zonemap is not locked for updates." );
			continue;
		}
		
		{
			GenesisRuntimeStatus* mapStatus;
			{
				ZoneMap* zmap = zmapLoad( zmapInfo );
				if( zmapInfoGetGenesisData( zmapInfo )->genesis_exterior ) { //sfenton TODO: this will have to be review if we ever have terrain and other in the same map
					zmapSetGenesisViewType( zmap, GENESIS_VIEW_NODES );
				}
				mapStatus = genesisReseedMapDesc( iPartitionIdx, zmap, seed_layout, seed_detail, zmapInfoGetPublicName( zmapInfo ));
				StructCopyAll( parse_ZoneMapInfo, zmapGetInfo( zmap ), zmapInfo );
				zmapUnload( zmap );
			}
		
			eaPushEArray( &genStatus->stages, &mapStatus->stages );
			eaClear( &mapStatus->stages );
			StructDestroy( parse_GenesisRuntimeStatus, mapStatus );
		}

		if( !zmapInfoSave( zmapInfo )) {
			genesisSetStageAndAdd(genStatus, "%s: Load/Save", zmapInfoGetPublicName( zmapInfo ));
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapInfo )), "Failed to save zone map." );
		}
	}
}

static GenesisRuntimeStatus* genesisFreezeExternalZMap(int iPartitionIdx, ZoneMap* zmap, bool forVistas, bool freezeIfJustWarnings)
{
	GenesisRuntimeStatus* genStatus = StructCreate( parse_GenesisRuntimeStatus );

	genesisSetErrorOnEncounter1( true ); 

	// First, make sure we have generated data.
	if( !forVistas ) {
		GenesisRuntimeStatus* status;
		status = genesisReseedMapDesc( iPartitionIdx, zmap, false, false, zmapInfoGetPublicName( zmapGetInfo( zmap )));
		eaPushEArray( &genStatus->stages, &status->stages );
		eaClear( &status->stages );
		StructDestroy( parse_GenesisRuntimeStatus, status );
	}

	genesisSetErrorOnEncounter1( false );
	
	if( genesisStatusHasErrors( genStatus, GENESIS_ERROR )) {
		return genStatus;
	}
	if( genesisStatusHasErrors( genStatus, 0 ) && !freezeIfJustWarnings ) {
		return genStatus;
	}

	{
		ZoneMap* prevActiveMap = worldGetActiveMap();

		// If worldCellSetEditable() gets called for the first time by
		// genesisGenerateTerrain(), there will almost certainly be a
		// crash because world_grid.maps does not contain the active
		// map.  This call ensures that worldCellSetEditable() has
		// been called at least once.
		//
		// Note: this crash was observed when running
		// /genesisFreezeVistaMaps.
		genesisWorldCellSetEditable();
	
		worldSetActiveMap( zmap );
		genesisGenerateTerrain( iPartitionIdx, zmap, true );
		worldSetActiveMap( prevActiveMap );
	}
	genesisGenerateMissionsOnServer( zmapGetInfo( zmap ));
	
	// save out a backup map desc
	if( !zmapInfoBackupMapDesc( zmapGetInfo( zmap ))) {
		genesisSetStageAndAdd( genStatus, "%s: Load/Save", zmapInfoGetPublicName( zmapGetInfo( zmap )));
		genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapGetInfo( zmap ) )), "Could not backup mapdescription." );
		return genStatus;
	}

	// Now commit it!
	zmapCommitGenesisData( zmap );

	return genStatus;
}

GenesisRuntimeStatus* genesisUnfreeze(ZoneMapInfo* zmapInfo)
{
	GenesisRuntimeStatus *gen_status = StructCreate( parse_GenesisRuntimeStatus );

	// Restore map desc
	if (!zmapInfoRestoreMapDescFromBackup(zmapInfo)) {
		genesisSetStageAndAdd( gen_status, "%s: Load/Save", zmapInfoGetPublicName( zmapInfo ));
		genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapInfo )), "Could not restore mapdescription." );
		return gen_status;
	}

	// Delete committed files on disk, but keep them backed up
	{
		char dir[MAX_PATH];
		char fullDir[MAX_PATH];
		char** files;
		strcpy(dir, zmapInfoGetFilename(zmapInfo));
		getDirectoryName(dir);

		fileLocateWrite(dir, fullDir);
		files = fileScanDir(fullDir);
		{
			int it;
			for( it = 0; it != eaSize(&files); ++it ) {
				if(   strEndsWith( files[it], ".layer" )
					  || strEndsWith( files[it], ".encounterlayer" )
					  || strEndsWith( files[it], ".layer.ms" )
					  || strEndsWith( files[it], ".encounterlayer.ms" )
					  || strEndsWith( files[it], ".alm" )
					  || strEndsWith( files[it], ".hmp" )
					  || strEndsWith( files[it], ".tiff" )
					  || strEndsWith( files[it], ".mat" )
					  || strEndsWith( files[it], ".soil" )
					  || strEndsWith( files[it], ".tom" )) {
					fileRenameToBak( files[it] );
				}
			}
		}
		fileScanDirFreeNames(files);
	}

	// Re-transmogrify!
	genesisTransmogrifyMapDesc(zmapInfo, gen_status, zmapInfoGetPublicName(zmapInfo));
	genesisGenerateMissionsOnServer( zmapInfo );

	return gen_status;
}

void genesisFreezeExternalMapDescs(int iPartitionIdx, GenesisRuntimeStatus* genStatus, ZoneMapInfo** zmapInfos, bool forVistas)
{
	int it;
	for( it = 0; it != eaSize( &zmapInfos ); ++it ) {
		ZoneMapInfo* zmapInfo = zmapInfos[ it ];

		if( !zmapInfoLocked( zmapInfo )) {
			genesisSetStageAndAdd( genStatus, "%s: Load/Save", zmapInfoGetPublicName( zmapInfo ));
			genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapInfo )), "Zonemap is not locked for updates." );
			continue;
		}
		
		{
			ZoneMap* zmap = zmapLoad( zmapInfo );
			{
				GenesisRuntimeStatus* mapStatus;
				mapStatus = genesisFreezeExternalZMap( iPartitionIdx, zmap, forVistas, false );
				StructCopyAll( parse_ZoneMapInfo, zmapGetInfo( zmap ), zmapInfo );
		
				eaPushEArray( &genStatus->stages, &mapStatus->stages );
				eaClear( &mapStatus->stages );
				StructDestroy( parse_GenesisRuntimeStatus, mapStatus );
			}
			zmapSaveLayers( zmap, NULL, true, false );

			if( !zmapInfoSave( zmapInfo )) {
				genesisSetStageAndAdd(genStatus, "%s: Load/Save", zmapInfoGetPublicName( zmapInfo ));
				genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapInfo )), "Failed to save zone map." );
			}
	
			zmapUnload( zmap );
		}
	}
}

void genesisUnfreezeExternalMapDescs(GenesisRuntimeStatus* genStatus, ZoneMapInfo** zmapInfos)
{
	int it;
	for( it = 0; it != eaSize( &zmapInfos ); ++it ) {
		ZoneMapInfo* zmapInfo = zmapInfos[ it ];

		if( !zmapInfoLocked( zmapInfo )) {
			genesisSetStageAndAdd( genStatus, "%s: Load/Save", zmapInfoGetPublicName( zmapInfo ));
			genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapInfo )), "Zonemap is not locked for updates." );
			continue;
		}
		
		{
			GenesisRuntimeStatus* mapStatus;
			mapStatus = genesisUnfreeze( zmapInfo );		
			eaPushEArray( &genStatus->stages, &mapStatus->stages );
			eaClear( &mapStatus->stages );
			StructDestroy( parse_GenesisRuntimeStatus, mapStatus );
		}

		if( !zmapInfoSave( zmapInfo )) {
			genesisSetStageAndAdd(genStatus, "%s: Load/Save", zmapInfoGetPublicName( zmapInfo ));
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapInfo )), "Failed to save zone map." );
		}
	}
}

void genesisSetExternalMapsType(GenesisRuntimeStatus* genStatus, ZoneMapInfo** zmapInfos, ZoneMapType zmtype)
{
	int it;
	for( it = 0; it != eaSize( &zmapInfos ); ++it ) {
		ZoneMapInfo* zmapInfo = zmapInfos[ it ];

		if( !zmapInfoLocked( zmapInfo )) {
			genesisSetStageAndAdd( genStatus, "%s: Load/Save", zmapInfoGetPublicName( zmapInfo ));
			genesisRaiseError( GENESIS_FATAL_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapInfo )), "Zonemap is not locked for updates." );
			continue;
		}
		
		zmapInfoSetMapType(zmapInfo, zmtype);
		if( !zmapInfoSave( zmapInfo )) {
			genesisSetStageAndAdd(genStatus, "%s: Load/Save", zmapInfoGetPublicName( zmapInfo ));
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zmapInfo )), "Failed to save zone map." );
		}
	}
}

void genesisGetSpawnPosition(int iPartitionIdx, WorldRegionType region_type, int idx, Vec3 spawn_pos_ret)
{
	RegionRules *pRules = getRegionRulesFromRegionType(region_type);
	Quat qRot = {0,0,0,-1};
	Entity_GetPositionOffset(iPartitionIdx, pRules, qRot, idx, spawn_pos_ret, NULL);
}

void genesisGenerate(int iPartitionIdx, ZoneMap *zmap, bool preview_mode, bool write_layers)
{
	GenesisZoneMapData *proc_data = zmapInfoGetGenesisData(zmapGetInfo(zmap));
	GenesisMissionRequirements **mission_reqs = NULL;
	int i;

	if (!isProductionMode())
		assertHeapValidateAll();

	sharedMemoryEnableEditorMode();
	for (i = 0; proc_data && i != eaSize(&proc_data->genesis_mission); ++i)
	{
		GenesisMissionRequirements *req;
		req = genesisGenerateMission(zmapGetInfo(zmap), i, proc_data->genesis_mission[ i ], NULL, false, "Genesis", false);
		if( !req ) {
			eaDestroyStruct( &mission_reqs, parse_GenesisMissionRequirements );
			return;
		}
		eaPush( &mission_reqs, req );
	}
	genesisGenerateGeometry(iPartitionIdx, zmap, mission_reqs, preview_mode, write_layers);

	eaDestroyStruct( &mission_reqs, parse_GenesisMissionRequirements );

	if (!isProductionMode())
		assertHeapValidateAll();
}

void genesisGenerateMissionsOnServer(ZoneMapInfo* zmapInfo)
{
	#ifdef GAMECLIENT
	{
		GenesisZoneMapData* genesisData = zmapInfoGetGenesisData(zmapInfo);
		Packet *pak;
	
		if (!genesisData || eaSize(&genesisData->genesis_mission) == 0 ||
			!(*editState.link) || linkDisconnected(*editState.link)) {
			return;
		}
	
		resSetDictionaryEditMode("FSM", true);
		resSetDictionaryEditMode("Mission", true);
		resSetDictionaryEditMode("Contact", true);
		resSetDictionaryEditMode("Message", true);		

		pak = pktCreate((*editState.link), editState.editCmd);
		pktSendBitsAuto(pak, ++editState.reqID);
		pktSendBitsAuto(pak, worldGetModTime());
		pktSendBitsAuto(pak, CommandGenesisGenerateMissions);
		pktSendStruct(pak, zmapInfo, parse_ZoneMapInfo);
		pktSend(&pak);
	}
	#else
	{
		genesisWriteAllMissions(zmapInfo);
	}
	#endif
}

void genesisGenerateEpisodeMissionOnServer(const char* episode_root, GenesisEpisode* episode)
{
	#ifdef GAMECLIENT
	{
		if ((*editState.link) && linkConnected(*editState.link))
		{
			Packet *pak = pktCreate((*editState.link), editState.editCmd);
			pktSendBitsAuto(pak, ++editState.reqID);
			pktSendBitsAuto(pak, worldGetModTime());
			pktSendBitsAuto(pak, CommandGenesisGenerateEpisodeMission);
			pktSendString(pak, episode_root);
			pktSendStruct(pak, episode, parse_GenesisEpisode);
			pktSend(&pak);
		}
	}
	#else
	{
		genesisGenerateEpisodeMission(episode_root, episode);
	}
	#endif
}

/// Utility function, compares if two GenesisMapDescriptions have the
/// same name.
static int genesisMapDescNameEq( const GenesisMapDescription* mapDesc1, const GenesisMapDescription* mapDesc2 )
{
	return stricmp( mapDesc1->name, mapDesc2->name ) == 0;
}

/// Create an suite of maps for EPISODE, stored all in EPISODE-ROOT.
GenesisRuntimeStatus* genesisCreateEpisode(int iPartitionIdx, GenesisEpisode* episode, const char* episode_root, U32 seed, U32 detail_seed)
{
	GenesisRuntimeStatus* genStatus = StructCreate( parse_GenesisRuntimeStatus );
	GenesisMapDescription** mapDescs = NULL;

	genesisSetStageAndAdd( genStatus, "Create Episode" );
	
	// write out the episode
	{
		char backupDescPath[ MAX_PATH ];
		sprintf( backupDescPath, "%s/backup_%s.episode", episode_root, episode->name );
		if( !ParserWriteTextFileFromSingleDictionaryStruct( backupDescPath, g_EpisodeDictionary, episode, 0, 0 )) {
			genesisRaiseErrorInternalCode( GENESIS_WARNING, "Could not write out backup episode file." );
		}
	}

	{
		GenesisEpisodePart* nextPart = NULL;
		int partIt;
		for( partIt = eaSize( &episode->parts ) - 1; partIt >= 0; --partIt ) {
			GenesisEpisodePart* part = episode->parts[ partIt ];
			GenesisMapDescription* mapDesc = GET_REF( part->map_desc );
			int mapDescIndex;

			if( !mapDesc ) {
				char buffer[32];
				sprintf( buffer, "%d", partIt + 1 );
				genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextEpisodePart(buffer),
									 "Part references MapDesc \"%s\", but no such MapDesc exists.",
									 partIt + 1, REF_STRING_FROM_HANDLE( part->map_desc ));
				continue;
			}

			if( (mapDescIndex = eaFindCmp( &mapDescs, mapDesc, genesisMapDescNameEq )) != -1 ) {
				mapDesc = mapDescs[ mapDescIndex ];
			} else {
				mapDesc = StructClone( parse_GenesisMapDescription, mapDesc );
				eaPush( &mapDescs, mapDesc );
			}

			assert( mapDesc );

			// ensure that corresponding mission is set properly to
			// interact with the Episode.
			{
				int missionIndex = genesisFindMission( mapDesc, part->mission_name );
				GenesisMissionDescription* mission;
				GenesisMissionStartDescription* startDesc;

				if( missionIndex < 0 ) {
					char buffer[32];
					sprintf( buffer, "%d", partIt + 1 );
					genesisRaiseError( GENESIS_ERROR, genesisMakeTempErrorContextEpisodePart(buffer),
										 "Part references mission \"%s\" in MapDesc \"%s\", but no such mission exists.",
										 part->mission_name, REF_STRING_FROM_HANDLE( part->map_desc ));
					continue;
				}
				mission = mapDesc->missions[ missionIndex ];
				startDesc = &mission->zoneDesc.startDescription;

				mission->zoneDesc.generationType = GenesisMissionGenerationType_OpenMission_NoPlayerMission;
				if( !mission->zoneDesc.pOpenMissionDescription ) {
					mission->zoneDesc.pOpenMissionDescription = StructCreate( parse_GenesisMissionOpenMissionDescription );
					mission->zoneDesc.pOpenMissionDescription->pcPlayerSpecificDisplayName = StructAllocString( mission->zoneDesc.pcDisplayName );
					mission->zoneDesc.pOpenMissionDescription->pcPlayerSpecificShortText = StructAllocString( mission->zoneDesc.pcShortText );
				}

				if( IS_HANDLE_ACTIVE( part->hStartTransitionOverride )) {
					COPY_HANDLE( startDesc->hStartTransitionOverride, part->hStartTransitionOverride );
				}

				if( nextPart ) {
					startDesc->bContinue = true;
					startDesc->eContinueFrom = part->eContinueFrom;
					StructFreeString( startDesc->pcContinueRoom );
					StructFreeString( startDesc->pcContinueChallenge );
					StructFreeString( startDesc->pcContinueMap );
					startDesc->pcContinueRoom = StructAllocString( part->pcContinueRoom );
					startDesc->pcContinueChallenge = StructAllocString( part->pcContinueChallenge );
					startDesc->pcContinueMap = StructAllocString( genesisEpisodePartMapName( episode, REF_STRING_FROM_HANDLE( nextPart->map_desc )));
					{
						WorldVariable* missionNumVar = StructCreate( parse_WorldVariable );
						missionNumVar->pcName = allocAddString( "Mission_Num" );
						missionNumVar->eType = WVAR_INT;
						missionNumVar->iIntVal = missionIndex;

						eaPush( &startDesc->eaContinueVariables, missionNumVar );
					}
					StructCopyAll( parse_GenesisMissionCostume, &part->continuePromptCostume, &startDesc->continuePromptCostume );
					StructCopyString( &startDesc->pcContinuePromptButtonText, part->pcContinuePromptButtonText );
					StructCopyString( &startDesc->pcContinuePromptCategoryName, part->pcContinuePromptCategoryName );
					startDesc->eContinuePromptPriority = part->eContinuePromptPriority;
					StructCopyString( &startDesc->pcContinuePromptTitleText, part->pcContinuePromptTitleText );
					eaCopyEx( &part->eaContinuePromptBodyText, &startDesc->eaContinuePromptBodyText, strdupFunc, strFreeFunc );
					
					if( IS_HANDLE_ACTIVE( part->hContinueTransitionOverride )) {
						COPY_HANDLE( startDesc->hContinueTransitionOverride, part->hContinueTransitionOverride );
					}
				}
			}

			nextPart = part;
		}
	}
	
	genesisSetStage(NULL);
	
	{
		int mapIt;
		for( mapIt = 0; mapIt != eaSize( &mapDescs ); ++mapIt ) {
			GenesisMapDescription* mapDesc = mapDescs[ mapIt ];
			const char* mapName = genesisEpisodePartMapName( episode, mapDesc->name );
			GenesisRuntimeStatus* mapStatus;
			
			char mapPath[ MAX_PATH ];
			sprintf( mapPath, "%s/%s/%s.zone", episode_root, mapDesc->name, mapName );
			
			mapStatus = genesisCreateExternalMap( iPartitionIdx, mapDesc, mapPath, mapName, NULL, seed,
												  detail_seed, false, false );
			eaPushEArray( &genStatus->stages, &mapStatus->stages );
			eaClear( &mapStatus->stages );
			StructDestroy( parse_GenesisRuntimeStatus, mapStatus );
		}
	}

	genesisSetStageAndAdd(genStatus, "Create Episode (Late)" );
	genesisGenerateEpisodeMissionOnServer( episode_root, episode );
	genesisSetStage( NULL );
	
	return genStatus;
}

GenesisRuntimeStatus *genesisCreateExternalMap(int iPartitionIdx, GenesisMapDescription *map_desc, const char *file_name, const char *map_name, const char *map_display_name, U32 seed, U32 detail_seed, bool move_to_nodes, bool force_create_zmap)
{
	GenesisRuntimeStatus *gen_status;
	ZoneMapInfo *zminfo = zmapInfoNew(file_name, map_name);
	GenesisZoneMapData *genesis_data = zmapInfoAddGenesisData(zminfo, 0);

	if( strstri(file_name, "/TestMaps/" )) {
		zmapInfoAddPrivacy(zminfo, NULL); // Make private to current user
	}
	zmapInfoSetMapType(zminfo, ZMTYPE_MISSION);

	assert(genesis_data && map_desc && file_name && map_name);
	genesis_data->map_desc = StructClone(parse_GenesisMapDescription, map_desc);
	genesis_data->seed = seed;
	genesis_data->detail_seed = detail_seed;
	
	// set the display name
	if( map_display_name ) {
		Message displayName = { 0 };
		displayName.pcDescription = "Autogenerated Planet Name";
		displayName.pcDefaultString = (char*)map_display_name;
		zmapInfoSetDisplayNameMessage( zminfo, &displayName );
	}

	if( move_to_nodes && map_desc->exterior_layout ) {
		bool failed = false;
		gen_status = StructCreate( parse_GenesisRuntimeStatus );

		if( !genesisTransmogrifyMapDesc( zminfo, gen_status, map_name )) {
			failed = true;
		}
		if (!failed) {
			genesisSetStageAndAdd(gen_status, "%s: Create External Map", map_name);
			genesisLoadDetailKitLibrary();
			genesis_data->genesis_exterior_nodes = genesisExteriorMoveRoomsToNodes(genesis_data->genesis_exterior, seed, detail_seed, true, true, true);
			
			if(!genesis_data->genesis_exterior_nodes)
			{
				failed = true;
				genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zminfo )), "Failed to generate nodes.");
			}
			else
			{
				Vec2 min_pos, max_pos;
				GenesisConfig* config = genesisConfig();
				int vista_thickness = SAFE_MEMBER(config, vista_thickness);
				if(vista_thickness <= 0)
					vista_thickness = GENESIS_EXTERIOR_DEFAULT_VISTA_THICKNESS;
				copyVec2(map_desc->exterior_layout->play_min, min_pos);
				copyVec2(map_desc->exterior_layout->play_max, max_pos);
				subVec2same(min_pos, vista_thickness*GRID_BLOCK_SIZE, min_pos);
				addVec2same(max_pos, vista_thickness*GRID_BLOCK_SIZE, max_pos);
				genesisMakeNodeBorders(genesis_data->genesis_exterior_nodes, NULL, min_pos, max_pos);
				StructDestroySafe(parse_GenesisZoneExterior, &genesis_data->genesis_exterior);
			}
		}
	} else {
		ZoneMap* zmap = zmapLoad(zminfo);
		if( zmapInfoGetGenesisData( zminfo )->genesis_exterior ) { //sfenton TODO: this will have to be review if we ever have terrain and other in the same map
			zmapSetGenesisViewType( zmap, GENESIS_VIEW_NODES );
		}
		gen_status = genesisReseedMapDesc( iPartitionIdx, zmap, false, false, map_name );
		StructCopyAll( parse_ZoneMapInfo, zmapGetInfo(zmap), zminfo );
		zmapUnload(zmap);
	}

	if( force_create_zmap || !genesisStatusFailed( gen_status ))
	{
		if( !zmapInfoSave( zminfo )) {
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextMap(zmapInfoGetPublicName( zminfo )), "Failed to save zone map.");
		}
	}
	else
	{
		genesisGenerateMissionsOnServer(zminfo);
	}

	genesisSetStage(NULL);

	return gen_status;
}

#endif
AUTO_COMMAND ACMD_CATEGORY(World, Interface) ACMD_NAME("Genesis.MakeVistas");
void genesisMakeVistas(Entity *pEnt, int count,
					   ACMD_NAMELIST("GenesisEcosystem", REFDICTIONARY) const char *ecosystem_name,
					   ACMD_NAMELIST("GenesisGeotype", REFDICTIONARY) const char *geotype_name)
{
#ifndef NO_EDITORS
	int i,j,k;
	GenesisEcosystem *ecosystem=NULL;
	GenesisGeotype *geotype=NULL;
	GenesisMapDescription *map_desc;
	char file_name[MAX_PATH];
	char map_name[MAX_PATH];
	int start_idx = 0;
	GenesisConfig* config = genesisConfig();
	int iPartitionIdx = entGetPartitionIdx(pEnt);
	int vista_hole_size = SAFE_MEMBER(config, vista_hole_size);


	if(vista_hole_size <= 0)
		vista_hole_size = GENESIS_EXTERIOR_DEFAULT_VISTA_HOLE_SIZE;

	loadstart_printf("Making Genesis Vistas...");

	//Load Ecosystem and Geotype
	ecosystem = RefSystem_ReferentFromString(GENESIS_ECOTYPE_DICTIONARY, ecosystem_name);
	geotype = RefSystem_ReferentFromString(GENESIS_GEOTYPE_DICTIONARY, geotype_name);
	if(!ecosystem)
	{
		Alertf("Could not find Ecosystem");
		return;
	}
	if(!geotype)
	{
		Alertf("Could not find Geotype");
		return;
	}

	//Find the starting index
	{
		ZoneMapInfo **zmap_list = NULL;
		sprintf(map_name, "%s_%s_%s_", GENESIS_EXTERIOR_VISTAS_FOLDER, ecosystem->name, geotype->name);
		worldGetZoneMapsThatStartWith(map_name, &zmap_list);
		for( i=0; i < eaSize(&zmap_list); i++ )
		{
			ZoneMapInfo *vista_zmap = zmap_list[i];
			const char *vista_pub_name = zmapInfoGetPublicName(vista_zmap);
			int val = atoi(vista_pub_name + strlen(map_name));
			if(val >= start_idx)
				start_idx = val+1;
		}
		eaDestroy(&zmap_list);
	}

	//Make a map description for processing
	map_desc = StructCreate(parse_GenesisMapDescription);
	map_desc->version = GENESIS_MAP_DESC_VERSION;
	map_desc->exterior_layout = StructCreate(parse_GenesisExteriorLayout);
	map_desc->exterior_layout->is_vista = true;
	setVec2same(map_desc->exterior_layout->play_min, 0);
	setVec2same(map_desc->exterior_layout->play_max, vista_hole_size*GRID_BLOCK_SIZE);
	SET_HANDLE_FROM_REFERENT(GENESIS_GEOTYPE_DICTIONARY, geotype, map_desc->exterior_layout->info.geotype);
	SET_HANDLE_FROM_REFERENT(GENESIS_ECOTYPE_DICTIONARY, ecosystem, map_desc->exterior_layout->info.ecosystem);
	//Add a room
	{
		GenesisLayoutRoom *room;
		GenesisRoomDef *room_def;
		room = StructCreate(parse_GenesisLayoutRoom);
		room->name = StructAllocString("Room1");
		room_def = RefSystem_ReferentFromString(GENESIS_ROOM_DEF_DICTIONARY, "TerrainVistaRoom");
		if(room_def)
			SET_HANDLE_FROM_REFERENT(GENESIS_ROOM_DEF_DICTIONARY, room_def, room->room);
		eaPush(&map_desc->exterior_layout->rooms, room);
	}

	//Make the vistas requested
	for( i=0; i < count; i++ )
	{
		GenesisRuntimeStatus *gen_status;
		U32 seed;

		sprintf(file_name, "maps/%s/%s/%s/%02d/%02d.zone", GENESIS_EXTERIOR_VISTAS_FOLDER, ecosystem->name, geotype->name, start_idx+i, start_idx+i);
		sprintf(map_name, "%s_%s_%s_%02d", GENESIS_EXTERIOR_VISTAS_FOLDER, ecosystem->name, geotype->name, start_idx+i);
		loadstart_printf("Making Vista: %s ...", map_name);

		seed = rand();
		gen_status = genesisCreateExternalMap(iPartitionIdx, map_desc, file_name, map_name, NULL, seed, seed, true, false);
		if (genesisStatusFailed(gen_status))
		{
			printf("\n");
			for( j=0; j < eaSize(&gen_status->stages); j++ )
			{
				GenesisRuntimeStage *stage = gen_status->stages[j];
				printf("%s\n", stage->name);
				for( k=0; k < eaSize(&stage->errors) ; k++ )
				{
					if(stage->errors[k]->type == GENESIS_FATAL_ERROR)
						printf("FATAL ERROR: %s\n", stage->errors[k]->message);
				}
			}
		}
		StructDestroy(parse_GenesisRuntimeStatus, gen_status);

		loadend_printf( "done." );
	}
	loadend_printf( "done." );

#endif
}
#ifndef NO_EDITORS

void genesisWriteAllMissions(ZoneMapInfo* zmap_info)
{
	GenesisZoneMapData* proc_data = zmapInfoGetGenesisData(zmap_info);
	GenesisZoneMission** genesis_missions = SAFE_MEMBER(proc_data, genesis_mission);
	int i;

	genesisDeleteMissions(zmapInfoGetFilename(zmap_info));
	for (i = 0; i != eaSize(&genesis_missions); ++i)
	{
		GenesisMissionRequirements* req;
		req = genesisGenerateMission(zmap_info, i, proc_data->genesis_mission[ i ], NULL, false, "Genesis", true);
		if( !req ) {
			return;
		}
		StructDestroy(parse_GenesisMissionRequirements, req);
	}
}

#endif
AUTO_RUN;
void genesisSetCallback(void)
{
#ifndef NO_EDITORS
	genesisSetWLGenerateFunc(genesisGenerate, genesisGenerateMissionsOnServer, genesisGenerateEpisodeMission, genesisGetSpawnPosition);
#endif
}
