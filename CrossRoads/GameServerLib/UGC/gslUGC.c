//// Shared GameServer interface from the rest of the code to UGC.
////
//// Deals with stuff like how to call generation, upload resources to
//// UGCMaster, etc.
#include "gslUGC.h"

#include "Entity.h"
#include "EntityLib.h"
#include "GameAccountDataCommon.h"
#include "GameServerLib.h"
#include "JobManagerSupport.h"
#include "Player.h"
#include "ServerLib.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "TransactionOutcomes.h"
#include "UGCCommon.h"
#include "UGCError.h"
#include "UGCProjectCommon.h"
#include "WorldGrid.h"
#include "allegiance.h"
#include "encounter_common.h"
#include "error.h"
#include "file.h"
#include "gslEntity.h"
#include "gslInteractable.h"
#include "gslMapVariable.h"
#include "gslSpawnPoint.h"
#include "gslUGCTransactions.h"
#include "gslUGC_cmd.h"
#include "logging.h"
#include "sysutil.h"
#include "textparser.h"
#include "trivia.h"
#include "utilitiesLib.h"
#include "windefinclude.h"

// MJF Oct/6/2012 -- This is a very evil way to get access to
// LibFileLoad.  We shouldn't be reaching into the private includes.
#include "StaticWorld/WorldGridLoadPrivate.h"

#include "AutoGen/AppServerLib_autogen_RemoteFuncs.h"
#include "AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

// Send an autosave if user enters Preview mode with unsaved changes and it's been at least 5 minutes since last autosave (user disconnect always makes an autosave)
#define UGC_SERVER_AUTOSAVE_TIMEOUT 5*60 

//globals used for gslUGC_TransferToMapWithDelay:
static int s_iNumFramesBeforeTransfer = 0;
static EntityRef s_entRefTransfer = 0;
static char *s_pcMapName = NULL;
static char *s_pcSpawnPoint = NULL;
static Vec3 s_vSpawnPos = { 0, 0, 0 };
static Vec3 s_vSpawnPYR = { 0, 0, 0 };
static bool s_bSpawnPosNonNull = 0;	//can't set the arrays above to NULL, so use this.
static UGCProjectData* s_pStartObjectiveProjData = NULL;
static int s_startObjectiveID = 0;

static UGCProjectData *pCurrentProjectData = NULL;
static bool bCurrentProjectDataDirty = false;
static U32 uCurrentProjectDataLastAutosaveTime = 0;

static char* gServerBinnerProjectInfo = NULL;
AUTO_CMD_ESTRING(gServerBinnerProjectInfo, ServerBinnerProjectInfo) ACMD_CMDLINE;

static int gServerBinnerDoRegenerate = false;
AUTO_CMD_INT(gServerBinnerDoRegenerate, ServerBinnerDoRegenerate) ACMD_CMDLINE;

char gUGCImportProjectName[128] = "";
AUTO_CMD_STRING(gUGCImportProjectName, UGCImportProjectOnStart) ACMD_CMDLINE;

bool g_UGCPublishAndQuit = false;
AUTO_CMD_INT(g_UGCPublishAndQuit, UGCPublishAndQuit) ACMD_CMDLINE;

int giUGCLeavePreviewStallySeconds = 120;
AUTO_CMD_INT(giUGCLeavePreviewStallySeconds, UGCLeavePreviewStallySeconds);
int giUGCPreviewTransferStallySeconds = 120;
AUTO_CMD_INT(giUGCPreviewTransferStallySeconds, UGCPreviewTransferStallySeconds);
int giUGCEnterPreviewStallySeconds = 120;
AUTO_CMD_INT(giUGCEnterPreviewStallySeconds, UGCEnterPreviewStallySeconds);
int giUGCSaveStallySeconds = 120;
AUTO_CMD_INT(giUGCSaveStallySeconds, UGCSaveStallySeconds);
int giUGCPublishStallySeconds = 120;
AUTO_CMD_INT(giUGCEnterPreviewStallySeconds, UGCPublishStallySeconds);

static bool ugcResourceCanPlayerEdit( Entity* player, const char* resourceName )
{
	// TODO: see if player has editing permisions.
	return isProductionEditMode() || !isProductionMode();
}

static bool ugcMapHasAllegiance(const char *map_name, const char *allegiance_name)
{
	ZoneMapEncounterInfo *zeniInfo;
	if (resNamespaceIsUGC(map_name))
		return true;
	zeniInfo = RefSystem_ReferentFromString( "ZoneMapEncounterInfo", map_name );
	if (!zeniInfo)
		return false;

	FOR_EACH_IN_EARRAY(zeniInfo->objects, ZoneMapEncounterObjectInfo, object_info)
	{
		if (IS_HANDLE_ACTIVE(object_info->displayName))
		{
			if (eaSize(&object_info->restrictions.eaFactions) == 0)
				return true;
			FOR_EACH_IN_EARRAY(object_info->restrictions.eaFactions, WorldUGCFactionRestrictionProperties, faction)
			{
				if (stricmp(faction->pcFaction, allegiance_name) == 0)
					return true;
			}
			FOR_EACH_END;
		}
	}
	FOR_EACH_END;
	return false;
}

static bool ugcResourceMapHasAllegiance( Entity* player, const char *mapName )
{
	AllegianceDef *allegiance = GET_REF(player->hAllegiance);
	AllegianceDef *subAllegiance = GET_REF(player->hSubAllegiance);
	if (allegiance && !ugcMapHasAllegiance(mapName, allegiance->pcName) &&
		(!subAllegiance || !ugcMapHasAllegiance(mapName, subAllegiance->pcName)))
	{
		return false;
	}
	return true;
}

//__CATEGORY UGC settings
// If zero, then playing UGC content is disabled. This is accomplished by GameServers returning empty UGC search results and players auto-dropping their UGC missions.
static bool s_bUGCPlayingEnabled = 1;
AUTO_CMD_INT(s_bUGCPlayingEnabled, UGCPlayingEnabled) ACMD_AUTO_SETTING(Ugc, GAMESERVER);

bool gslUGC_PlayingIsEnabled()
{
	return !!s_bUGCPlayingEnabled;
}

//__CATEGORY UGC settings
// If zero, then adding or changing UGC mission reviews is disabled. 
static int giUGCReviewingEnabled = 1;
AUTO_CMD_INT(giUGCReviewingEnabled, UGCReviewingEnabled) ACMD_AUTO_SETTING(Ugc, GAMESERVER);

bool gslUGC_ReviewingIsEnabled()
{
	return(giUGCReviewingEnabled!=0);
}

AUTO_COMMAND_REMOTE ACMD_INTERSHARD;
void gslUGC_RequestDetails_Return(ContainerID entContainerID, UGCDetails *pDetails, S32 iRequesterID)
{
	Entity *pEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, entContainerID);
	if(pEnt)
		ClientCmd_gclUGC_ReceiveDetails(pEnt, pDetails, 0, 0, gslUGC_ReviewingIsEnabled(), iRequesterID);
}

void gslUGC_DoRequestDetails(Entity *pEnt, U32 iProjectID, U32 iSeriesID, S32 iRequesterID)
{
	RemoteCommand_Intershard_aslUGCDataManager_RequestDetails(ugc_ShardName(), GLOBALTYPE_UGCDATAMANAGER, 0,
		GetShardNameFromShardInfoString(), entGetContainerID(pEnt), iProjectID, iSeriesID, entGetAccountID(pEnt), iRequesterID);
}

void gslUGC_AddTriviaData( UGCProjectData* data )
{
	char* buffer = NULL;
	ParserWriteText( &buffer, parse_UGCProjectData, data, 0, 0, 0 );
	triviaPrintf( "UGC", "%s", buffer );
	estrDestroy( &buffer );
}

void gslUGC_RemoveTriviaData( void )
{
	triviaRemoveEntry( "UGC" );
}

/// Update the global copy of the current editing project for autosave
void gslUGC_SetUGCProjectCopy(UGCProjectData *data)
{
	if (!pCurrentProjectData)
		pCurrentProjectData = StructCreate(parse_UGCProjectData);

	if (data)
	{
		// TomY TODO - validate namespace
		if (StructCompare(parse_UGCProjectData, data, pCurrentProjectData, 0, 0, 0) != 0)
		{
			StructCopy(parse_UGCProjectData, data, pCurrentProjectData, 0, 0, 0);
			bCurrentProjectDataDirty = true;
			log_printf( LOG_UGC, "%s: Updated project copy", __FUNCTION__ );
		}
	}
	else
	{
		StructReset(parse_UGCProjectData, pCurrentProjectData);
		bCurrentProjectDataDirty = false;
		log_printf( LOG_UGC, "%s: Cleared project copy", __FUNCTION__ );
	}
}

/// We just completed a save, so clear the dirty flag
void gslUGC_ClearUGCProjectCopyDirtyFlag(void)
{
	bCurrentProjectDataDirty = false;
	uCurrentProjectDataLastAutosaveTime = timeSecondsSince2000();
}

/// Either the client disconnected, or we're about to do something crash-prone
void gslUGC_SendAutosaveIfNecessary(bool force)
{
	if (bCurrentProjectDataDirty)
	{
		char filename[MAX_PATH];
		UGCProjectAutosaveData pWriteData = { 0 };
		bool succeeded;
		char *pcError = NULL;

		if (!force && uCurrentProjectDataLastAutosaveTime > 0 && timeSecondsSince2000() - uCurrentProjectDataLastAutosaveTime < UGC_SERVER_AUTOSAVE_TIMEOUT)
		{
			log_printf( LOG_UGC, "%s: Skipping autosave -- only %d seconds since last save.", __FUNCTION__, timeSecondsSince2000() - uCurrentProjectDataLastAutosaveTime );
			return;
		}

		log_printf( LOG_UGC, "%s: Starting Autosave, ns=%s",
					__FUNCTION__, ugcProjectDataGetNamespace( pCurrentProjectData ));

		assert(pCurrentProjectData);

		TellControllerWeMayBeStallyForNSeconds(giUGCSaveStallySeconds, "UGCAutosave");

		pWriteData.iTimestamp = timeSecondsSince2000();
		pWriteData.pData = pCurrentProjectData;

		// Write autosave file
		sprintf(filename, "ns/%s/autosave/autosave.ugcproject", ugcProjectDataGetNamespace(pCurrentProjectData));
		succeeded = ParserWriteTextFile(filename, parse_UGCProjectAutosaveData, &pWriteData, 0, 0);

		if (!gslUGC_UploadAutosave(ugcProjectDataGetNamespace(pCurrentProjectData), &pcError))
		{
			AssertOrAlert("UGC_AUTOSAVE_UPLOAD_FAILED", "Autosave upload failed. May be a problem with ugcmaster: %s", pcError);
		}

		gslUGC_ClearUGCProjectCopyDirtyFlag();

		// Clear files from disk
		gslUGC_DeleteNamespaceDataFiles(ugcProjectDataGetNamespace(pCurrentProjectData));

		log_printf( LOG_UGC, "%s: Uploaded autosave, ns=%s",
					__FUNCTION__, ugcProjectDataGetNamespace( pCurrentProjectData ));
	}
	else
	{
		log_printf( LOG_UGC, "%s: No autosave necessary", __FUNCTION__ );
	}
}

static void gslUGC_TransferLeaveMap(Entity *pEntity)
{
	// Despawn pets (if any) before transfer and otherwise consider the player to have left the previous map
	gslPlayerLeftMap(pEntity, true);
}

#define SAFE_AREF( a, i ) (a ? a[i] : 0)

static void gslUGC_TransferArriveAtMap(Entity *pEntity, const char *map_name, const char *spawn_point, Vec3 spawn_pos, Vec3 spawn_pyr)
{
	GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pEntity);
	
	log_printf( LOG_UGC, "%s: map_name=%s, spawn_point=%s, spawn_pos=%f,%f,%f, spawn_pyr=%f,%f,%f",
				__FUNCTION__, map_name, spawn_point,
				SAFE_AREF( spawn_pos, 0 ), SAFE_AREF( spawn_pos, 1 ), SAFE_AREF( spawn_pos, 2 ),
				SAFE_AREF( spawn_pyr, 0 ), SAFE_AREF( spawn_pyr, 1 ), SAFE_AREF( spawn_pyr, 2 ));

	//inform controller that we may be stalling for a while
	TellControllerWeMayBeStallyForNSeconds(giUGCPreviewTransferStallySeconds, "PreviewTransfer");

	// Load the new map
	worldLoadZoneMapByName(map_name);

	log_printf( LOG_UGC, "%s: After worldLoadZoneMapByName(%s)", __FUNCTION__, map_name );

	// Set the region and move the player
	if (spawn_point)
	{
		spawnpoint_MovePlayerToNamedSpawn(pEntity, spawn_point, NULL, 0);
	}
	else if (spawn_pos)
	{
		Quat spawn_rot = { 0, 0, 0, 1 };
		if(spawn_pyr)
			PYRToQuat(spawn_pyr, spawn_rot);
		spawnpoint_MovePlayerToLocation(pEntity, spawn_pos, spawn_rot, NULL, true);
	}
	else
	{
		spawnpoint_MovePlayerToStartSpawn(pEntity, true);
	}
	gslCacheEntRegion(pEntity,pExtract);

	// Puppet swap the player if necessary
	// Spawn away team pets if necessary
	// Do all other map entry stuff
	gslPlayerEnteredMap(pEntity, false);

	// End loading screen on client
	ClientCmd_gclLoading_ClearForcedLoading(pEntity);
}

// Sets up to do the map transfer in 5 frames.  Saves inputs into globals and gslUGC_Tick() calls 
// gslUGC_TransferArriveAtMap() with them in 5 frames.  The delay is so that the game server 
// has time to load the map before trying to move the player there.
void gslUGC_TransferToMapWithDelay(Entity *pEntity, const char *map_name, const char *spawn_point, const Vec3 spawn_pos, const Vec3 spawn_pyr, UGCProjectData* start_objective_proj, int start_objective_id)
{
	log_printf( LOG_UGC, "%s: map_name=%s, spawn_point=%s, spawn_pos=%f,%f,%f, spawn_pyr=%f,%f,%f",
				__FUNCTION__, map_name, spawn_point,
				SAFE_AREF( spawn_pos, 0 ), SAFE_AREF( spawn_pos, 1 ), SAFE_AREF( spawn_pos, 2 ),
				SAFE_AREF( spawn_pyr, 0 ), SAFE_AREF( spawn_pyr, 1 ), SAFE_AREF( spawn_pyr, 2 ));
	
	// Do the pre-leave actions
	gslUGC_TransferLeaveMap(pEntity);

	// Put up loading screen on client
	ClientCmd_gclLoading_SetForcedLoading(pEntity);

	// Set globals to cause delayed transfer
	s_iNumFramesBeforeTransfer = 5;

	s_entRefTransfer = entGetRef( pEntity );

	estrCopy2(&s_pcMapName, map_name);
	estrCopy2(&s_pcSpawnPoint, spawn_point);

	if(s_bSpawnPosNonNull = spawn_pos && spawn_pyr)
	{
		copyVec3(spawn_pos, s_vSpawnPos);
		copyVec3(spawn_pyr, s_vSpawnPYR);
	}

	if( start_objective_proj && start_objective_id ) {
		if( !s_pStartObjectiveProjData ) {
			s_pStartObjectiveProjData = StructCreate( parse_UGCProjectData );
		}
		StructCopyAll( parse_UGCProjectData, start_objective_proj, s_pStartObjectiveProjData );
		s_startObjectiveID = start_objective_id;
	} else {
		StructDestroySafe( parse_UGCProjectData, &s_pStartObjectiveProjData );
		s_startObjectiveID = 0;
	}
}

void gslUGC_Tick(void)
{
	if (s_iNumFramesBeforeTransfer > 0)
	{
		--s_iNumFramesBeforeTransfer;
		if (!s_iNumFramesBeforeTransfer)
		{
			Entity* entTransfer = entFromEntityRefAnyPartition( s_entRefTransfer );
			if( entTransfer ) {
				if(s_bSpawnPosNonNull)
				{
					gslUGC_TransferArriveAtMap(entTransfer, s_pcMapName, s_pcSpawnPoint, s_vSpawnPos, s_vSpawnPYR);
				}
				else
				{
					gslUGC_TransferArriveAtMap(entTransfer, s_pcMapName, s_pcSpawnPoint, NULL, NULL);				
				}
				ugcMissionStartObjective( entTransfer, s_pStartObjectiveProjData, s_pcMapName, s_startObjectiveID, gslUGC_DoPlayCB, (UserData)(intptr_t)s_entRefTransfer);
			}

			s_bSpawnPosNonNull = 0;
			s_entRefTransfer = 0;
			s_pcMapName = NULL;
			StructDestroySafe( parse_UGCProjectData, &s_pStartObjectiveProjData );
			s_startObjectiveID = 0;
		}
	}
}

void gslUGC_DoPlayCB( bool succeeded, UserData entityRef )
{
	Entity* pEntity = entFromEntityRefAnyPartition( (EntityRef)(intptr_t)entityRef );
	UGCPlayResult res = { 0 };
	const char* map_name = zmapInfoGetPublicName(NULL); 
	LibFileLoad **layer_cache;
	char **sky_cache;

	if( !pEntity ) {
		return;
	}
	if (!succeeded)
	{
		ClientCmd_gclUGCProcessPlayResult(pEntity, NULL);
		return;
	}
	
	// Success! Notify the client
	res.pInfo = zmapGetInfo(worldGetActiveMap());
	res.fstrFilename = zmapInfoGetFilename(res.pInfo);

	layer_cache = ugcLayerCacheGetAllLayers();
	FOR_EACH_IN_EARRAY(layer_cache, LibFileLoad, layer_data)
	{
		UGCPlayLayerData *new_data = StructCreate(parse_UGCPlayLayerData);
		new_data->filename = allocAddFilename(layer_data->filename);
		eaCopyStructs(&layer_data->defs, &new_data->eaDefs, parse_GroupDef);
		eaPush(&res.eaLayerDatas, new_data);
	}
	FOR_EACH_END;

	gslUGC_ProjectAddPlayIDEntryNames( gGSLState.pLastPlayData, &res.eaIDEntryNames );

	sky_cache = ugcLayerCacheGetAllSkies();
 	FOR_EACH_IN_EARRAY(sky_cache, char, sky)
	{
		eaPush(&res.eaSkyDefs, strdup(sky));
	}
	FOR_EACH_END;

	ClientCmd_gclUGCProcessPlayResult(pEntity, &res);
	eaDestroyStruct(&res.eaLayerDatas, parse_UGCPlayLayerData);
	eaDestroyStruct(&res.eaIDEntryNames, parse_UGCPlayIDEntryName);
}

void gslUGC_DoPlay( Entity *pEntity, UGCProjectData* ugc_proj, const char *map_name, U32 objective_id, Vec3 spawn_pos, Vec3 spawn_rot, bool only_reload )
{
	const char *spawn_name = NULL;
	log_printf( LOG_UGC, "%s: Start, ns=%s, map_name=%s, objective_id=%u, spawn_pos=%f,%f,%f, only_reload=%d",
				__FUNCTION__, ugcProjectDataGetNamespace( ugc_proj ), map_name, objective_id,
				SAFE_AREF( spawn_pos, 0 ), SAFE_AREF( spawn_pos, 1 ), SAFE_AREF( spawn_pos, 2 ),
				only_reload );

	TellControllerWeMayBeStallyForNSeconds(giUGCEnterPreviewStallySeconds, "EnterPreview");

	gslUGC_SendAutosaveIfNecessary(false);

	if( spawn_pos ) {
		gslUGCPlayPreprocess( ugc_proj, &map_name, &objective_id, NULL );
		spawn_name = "UGC_SPAWN_OVERRIDE";
	} else {
		gslUGCPlayPreprocess( ugc_proj, &map_name, &objective_id, &spawn_name );
	}		
	
	if (nullStr(map_name) || !ugcResourceCanPlayerEdit(pEntity, map_name))
	{
		UGCPlayResult res = { UGC_PLAY_NO_OBJECTIVE_MAP };
		ClientCmd_gclUGCProcessPlayResult(pEntity, &res);
		return;
	}
	if (ugcIsAllegianceEnabled())
	{
		AllegianceDef *allegiance = GET_REF(pEntity->hAllegiance);
		AllegianceDef *subAllegiance = GET_REF(pEntity->hSubAllegiance);
		UGCProjectInfo* project = ugcProjectDataGetProjectInfo(ugc_proj);
		if (project->pRestrictionProperties)
		{
			if (eaSize(&project->pRestrictionProperties->eaFactions) > 0)
			{
				bool found = false;
				FOR_EACH_IN_EARRAY(project->pRestrictionProperties->eaFactions, WorldUGCFactionRestrictionProperties, faction)
				{
					if (stricmp(faction->pcFaction, allegiance->pcName) == 0 ||
						stricmp(faction->pcFaction, subAllegiance->pcName) == 0)
					{
						found = true;
					}
				}
				FOR_EACH_END;
				if (!found)
				{
					UGCPlayResult res = { UGC_PLAY_WRONG_FACTION };
					ClientCmd_gclUGCProcessPlayResult(pEntity, &res);
					return;
				}
			}
		}
		if (!ugcResourceMapHasAllegiance(pEntity, map_name))
		{
			UGCPlayResult res = { UGC_PLAY_WRONG_FACTION };
			ClientCmd_gclUGCProcessPlayResult(pEntity, &res);
			return;
		}
	}

	if (ugc_proj != gGSLState.pLastPlayData)
	{
		StructDestroySafe(parse_UGCProjectData, &gGSLState.pLastPlayData);
		gGSLState.pLastPlayData = StructClone(parse_UGCProjectData, ugc_proj);
	}

	if( !only_reload ) {
		TellControllerToLog( __FUNCTION__ ": About to generate." );
		if (!ugcProjectGenerateOnServerEx(ugc_proj, /*override_spawn_map=*/map_name, /*override_spawn_pos=*/spawn_pos, /*overide_spawn_rot=*/spawn_rot))
		{
			UGCPlayResult res = { UGC_PLAY_GENESIS_GENERATION_ERROR };
			ClientCmd_gclUGCProcessPlayResult(pEntity, &res);
			TellControllerToLog( __FUNCTION__ ": Generate failed." );
			return;
		}
		log_printf( LOG_UGC, "%s: After ugcProjectGenerateOnServerEx(), ns=%s",
					__FUNCTION__, ugcProjectDataGetNamespace( ugc_proj ));
		TellControllerToLog( __FUNCTION__ ": Generate succeeded." );
		gslUGC_TransferToMapWithDelay(pEntity, map_name, spawn_name, NULL, NULL, ugc_proj, objective_id);

		// set variable overrides before we load the map so the variables get inited correctly
		eaClearStruct( &g_eaMapVariableOverrides, parse_WorldVariable );
		if( resNamespaceIsUGC( map_name )) {
			WorldVariable* missionNumVar = StructCreate( parse_WorldVariable );
			WorldVariable* baseLevelVar = StructCreate( parse_WorldVariable );

			missionNumVar->pcName = allocAddString( "Mission_Num" );
			missionNumVar->eType = WVAR_INT;
			missionNumVar->iIntVal = 1;
			eaPush( &g_eaMapVariableOverrides, missionNumVar );

			baseLevelVar->pcName = allocAddString( "BaseLevel" );
			baseLevelVar->eType = WVAR_INT;
			baseLevelVar->iIntVal = encounter_getTeamLevelInRange( pEntity, NULL, false );
			eaPush( &g_eaMapVariableOverrides, baseLevelVar );
		}
	} else {
		if (spawn_name)
			spawnpoint_MovePlayerToNamedSpawn(pEntity, spawn_name, NULL, 0);
		else
			spawnpoint_MovePlayerToStartSpawn(pEntity, true);
		
		ugcMissionStartObjective(pEntity, ugc_proj, map_name, objective_id, gslUGC_DoPlayCB, (UserData)(intptr_t)entGetRef( pEntity ));
	}

	gGSLState.bCurrentlyInUGCPreviewMode = true;
}

AUTO_STARTUP(UGCServer) ASTRT_DEPS(UGCReporting, UGCSearchCache);
void ugcServer_Load(void)
{
	ugcResourceLoadLibrary();
}

void gslUGC_HandleUserDisconnect(void)
{
	UGCProject *pProject = GET_REF(gGSLState.hUGCProjectFromSubscription);
	if (pProject)
	{
		FOR_EACH_IN_EARRAY(pProject->ppProjectVersions, UGCProjectVersion, version)
		{
			gslUGC_DeleteNamespaceDataFiles(version->pNameSpace);
		}
		FOR_EACH_END;
	}

	gslUGC_SendAutosaveIfNecessary(true);
}



static void gslUGC_MapValidateUGCGroup( ZoneMapEncounterUGCInfo* ugcInfo, ZoneMapEncounterObjectUGCGroup* group )
{
	WorldZoneMapScope *pScope = zmapGetScope( NULL );
	int it;

	assert( pScope );
	for( it = 0; it != eaSize( &group->objects ); ++it ) {
		ZoneMapEncounterObjectUGCInfo* zeniObject = group->objects[ it ];
		WorldEncounterObject* worldObject = worldScopeGetObject( &pScope->scope, zeniObject->logicalName );

		if( !worldObject ) {
			ErrorFilenamef( ugcInfo->filename, "UGC Object %s no longer exists!  This is almost certainly an error in a layer where a bunch of objects were recently deleted, and not an error in this file.  You can never, ever, ever remove a UGC object from a live game.",
							zeniObject->logicalName );
			continue;
		}
			
		if( !IS_HANDLE_ACTIVE( zeniObject->displayName )) {
			ErrorFilenamef( ugcInfo->filename, "UGC Object %s does not have DisplayName set.", zeniObject->logicalName );
		}
		if( !IS_HANDLE_ACTIVE( zeniObject->displayDetails )) {
			ErrorFilenamef( ugcInfo->filename, "UGC Object %s does not have DisplayDetails set.", zeniObject->logicalName );
		}
	}
}

void gslUGC_MapValidate( void )
{
	WorldZoneMapScope *pScope = zmapGetScope( NULL ); 
	
	if( pScope ) {
		ZoneMapEncounterInfo* pEncounterInfo = RefSystem_ReferentFromString( "ZoneMapEncounterInfo", zmapGetName( NULL ));
		const char* zmapFilename = zmapGetFilename( NULL );
		ZoneMapEncounterUGCInfo ugcInfo = { 0 };

		if( !zmapFilename ) {
			FatalErrorf( "Did not have an active map filename... this should never happen." );
			return;
		}
		{
			char ugcFilename[ MAX_PATH ];
			changeFileExt( zmapFilename, ".ugcinfo", ugcFilename );
			if( !ParserReadTextFile( ugcFilename, parse_ZoneMapEncounterUGCInfo, &ugcInfo, 0 )) {
				return;
			}
		}

		if( ugcInfo.deprecated_map_new_map_name ) {
			if(   eaSize( &ugcInfo.volume_logical_name ) > 0 || eaSize( &ugcInfo.groups ) > 0
				  || eaSize( &ugcInfo.default_group.objects ) > 0 ) {
				ErrorFilenamef( ugcInfo.filename, "UGCInfo specifies both DeprecatedMapNewMapName and volumes or objects.  If DeprecatedMapNewMapName is set, nothing else must be set." );
			}
		} else {
			int it;
			for( it = 0; it != eaSize( &ugcInfo.volume_logical_name ); ++it ) {
				const char* volumeName = ugcInfo.volume_logical_name[ it ];
				WorldEncounterObject* worldObject = worldScopeGetObject( &pScope->scope, volumeName );

				if( !worldObject || worldObject->type != WL_ENC_NAMED_VOLUME ) {
					ErrorFilenamef( ugcInfo.filename, "UGCInfo mentions volume %s, but that volume does not exist!",
									volumeName );
				}
			}

			gslUGC_MapValidateUGCGroup( &ugcInfo, &ugcInfo.default_group );
			for( it = 0; it != eaSize( &ugcInfo.groups ); ++it ) {
				gslUGC_MapValidateUGCGroup( &ugcInfo, ugcInfo.groups[ it ]);
			}
		}

		StructReset( parse_ZoneMapEncounterUGCInfo, &ugcInfo );
	}
}

UGCProjectAutosaveData *gslUGC_LoadAutosave(const char *namespace)
{
	char filename[MAX_PATH];
	UGCProjectAutosaveData *pRetData = StructCreate(parse_UGCProjectAutosaveData);
	sprintf(filename, "ns/%s/autosave/autosave.ugcproject", namespace);
	ParserReadTextFile(filename, parse_UGCProjectAutosaveData, pRetData, 0);
	return pRetData;
}

bool gslUGC_ServerBinnerGenerateProject(char *ns, bool *regenerated)
{
	UGCProjectData *ugcProj = NULL;
	UGCRuntimeStatus *validateRet = NULL;
	UGCProjectInfo *pUGCProjectInfo = StructCreate(parse_UGCProjectInfo);
	char *estrProjectInfo = NULL;
	bool ret = false;

	if (!gServerBinnerDoRegenerate)
		return true;

	loadstart_printf("Regenerating UGC project...");

	ugcResourceInfoPopulateDictionary();

	if (gServerBinnerProjectInfo &&
		estrSuperUnescapeString(&estrProjectInfo, gServerBinnerProjectInfo) &&
		ParserReadText(estrProjectInfo, parse_UGCProjectInfo, pUGCProjectInfo, 0))
	{
		int numDialogsDeleted = 0, numCostumesReset = 0, numObjectivesReset = 0;
		UGCProjectData *ugcProjCopy;

		estrDestroy(&estrProjectInfo);	// We're done with it
		
		ugcProj = gslUGC_LoadProjectDataWithInfo(ns, pUGCProjectInfo);				// ugcProj can never be NULL;
		ugcProjCopy = StructClone(parse_UGCProjectData, ugcProj);

		ugcEditorFixupProjectData(ugcProj, &numDialogsDeleted, &numCostumesReset, &numObjectivesReset, NULL);
		ugcProjectFixupDeprecated(ugcProj, true);
	
		if (numDialogsDeleted > 0 || numCostumesReset > 0 || numObjectivesReset > 0)
		{
			char *diff = NULL;
			StructWriteTextDiff(&diff, parse_UGCProjectData, ugcProj, ugcProjCopy, 0, 0, 0, 0);
			JobManagerUpdate_Log("Regenerate failed: %d dialogs deleted, %d costumes reset, %d objectives reset - we expect none. (DIFF: %s)",
									 numDialogsDeleted, numCostumesReset, numObjectivesReset, diff);
			estrDestroy(&diff);
			StructDestroy(parse_UGCRuntimeStatus, validateRet);
			StructDestroy(parse_UGCProjectData, ugcProj);
			StructDestroy(parse_UGCProjectData, ugcProjCopy);
			return false;
		}
	
		StructDestroy(parse_UGCProjectData, ugcProjCopy);

		// Validate the project
	
		ugcSetIsRepublishing(true);
		validateRet = StructCreate(parse_UGCRuntimeStatus);
		ugcSetStageAndAdd(validateRet, "UGC Validate");
		ugcValidateProject(ugcProj);
		ugcClearStage();
		ugcSetIsRepublishing(false);
	
		if (!ugcStatusHasErrors(validateRet, UGC_ERROR))
		{
			ugcProjectGenerateOnServer(ugcProj);
			ret = true;
			*regenerated = true;
	
			JobManagerUpdate_Log("Regenerate succeeded.");
		}
		else
		{
			int error_count = 0;
			char *error_str = NULL;
			UGCRuntimeError *important_error = ugcStatusMostImportantError(validateRet);

			ParserWriteText(&error_str, parse_UGCRuntimeError, important_error, 0, 0, 0);
			error_count = ugcStatusErrorCount( validateRet );
	
			JobManagerUpdate_Log("Regenerate failed with %d errors. Most important: %s.", error_count, error_str);
	
			estrDestroy(&error_str);
		}
	
		StructDestroy(parse_UGCRuntimeStatus, validateRet);
		StructDestroy(parse_UGCProjectData, ugcProj);
	}
	else
	{
		StructDestroy(parse_UGCProjectInfo, pUGCProjectInfo);
		JobManagerUpdate_Log("Regenerate failed to read project info.");
	}

	loadend_printf(" done.");

	return ret;
}

//////////////////////////////////////////////////////////////////////
// Called when the game server first starts up and is done loading
// everything.
void gslUGC_ServerIsDoneLoading( void )
{
	if( g_UGCPublishAndQuit ) {
		UGCProjectData* pProjectData = NULL;
		UGCProjectInfo* pProjectInfo = NULL;
		const UGCProjectVersion* pVersion;
		UGCProject *active_project = GET_REF(gGSLState.hUGCProjectFromSubscription);
		assert(active_project);
		assert( gUGCImportProjectName );
		
		// Load UGC resource infos so we can validate projects
		ugcResourceInfoPopulateDictionary();

		pVersion = UGCProject_GetMostRecentVersion(active_project);

		// Set up our derived project info to replace possibly outdated stuff that will be loaded
		pProjectInfo = ugcCreateProjectInfo(active_project, pVersion);

		// Load & send the actual project data
		if (gUGCImportProjectName && gUGCImportProjectName[0])
		{
			pProjectData = gslUGC_LoadProjectDataWithInfo(gUGCImportProjectName, pProjectInfo);
			gslUGC_RenameProjectNamespace(pProjectData, pVersion->pNameSpace);
			gslUGC_DoSave(pProjectData, NULL, NULL, false, NULL, __FUNCTION__);
		}
		
		SaveAndPublishUGCProject( NULL, pProjectData );
	}
}

static bool bDoNotDeleteNamespaceFiles = false;
AUTO_CMD_INT(bDoNotDeleteNamespaceFiles, DoNotDeleteNamespaceFiles);

void gslUGC_DeleteNamespaceDataFiles(const char *ns)
{
	char dir[MAX_PATH], out_dir[MAX_PATH];
	char cmd[MAX_PATH];
	int rv = 0;
	int try;

	if (!isProductionMode() || bDoNotDeleteNamespaceFiles)
		return;

	sprintf(dir, "ns/%s", ns);
	fileLocateWrite(dir, out_dir);
	if (!dirExists(out_dir))
		return;

	backSlashes(out_dir);
	sprintf(cmd, "rd /s /q %s", out_dir);
	TellControllerToLog( __FUNCTION__ ": About to do system command to delete files." );
	for (try = 0; try < 10; try++)
	{
		rv = system(cmd);
		if (rv == 0)
			break;
		Sleep(100);
	}
	TellControllerToLog( __FUNCTION__ ": System command finished." );
	
	if (rv != 0)
	{
		RunHandleExeAndAlert("UGC_NAMESPACE_FILE_DELETE_FAILED", out_dir, "log_namespace_delete", "Failed to remove namespace directory");

/*		sprintf(cmd, "handle %s > %slog_namespace_delete.log", out_dir, logGetDir());
		system(cmd);
		AssertOrAlert("UGC_NAMESPACE_FILE_DELETE_FAILED", "Failed to remove namespace directory: %s. Logged in %slog_namespace_delete.log", out_dir, logGetDir());
*/	}
}
