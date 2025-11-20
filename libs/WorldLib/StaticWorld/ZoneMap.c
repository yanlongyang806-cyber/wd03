/***************************************************************************



***************************************************************************/
#define GENESIS_ALLOW_OLD_HEADERS

#include "crypt.h"
#include "timing.h"
#include "fileutil.h"
#include "StringCache.h"
#include "qsortG.h"
#include "error.h"
#include "gimmeDLLWrapper.h"

#include "UGCProjectUtils.h"
#include "WorldCellStreaming.h"
#include "WorldCellStreamingPrivate.h"
#include "WorldGridLoadPrivate.h"
#include "WorldGridPrivate.h"
#include "wlEncounter.h"
#include "wlGenesis.h"
#include "wlGenesisMissions.h"
#include "wlState.h"
#include "wlTerrainBrush.h"
#include "RoomConn.h"

#include "MapDescription.h"
#include "wcoll/collcache.h"
#include "ContinuousBuilderSupport.h"


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

//////////////////////////////////////////////////////////////////
// Reference Dictionary
//////////////////////////////////////////////////////////////////

DictionaryHandle g_ZoneMapDictionary = NULL;
DictionaryHandle g_ZoneMapEncounterInfoDictionary = NULL;
DictionaryHandle g_ZoneMapExternalMapSnapDictionary = NULL;

static bool g_StripGenesisData = false;
static bool g_StripExtraData = false;

bool DEFAULT_LATELINK_isValidRegionTypeForGame(U32 world_region_type)
{
	return (world_region_type == WRT_Ground) || 
		   (world_region_type == WRT_CharacterCreator) || 
		   (world_region_type == WRT_None);
}

static ExprFuncTable* zmapCreateExprFuncTable(void)
{
	static ExprFuncTable* s_stFuncTable = NULL;
	if(!s_stFuncTable)
	{
		s_stFuncTable = exprContextCreateFunctionTable("ZoneMap");
		exprContextAddFuncsToTableByTag(s_stFuncTable, "util");
		exprContextAddFuncsToTableByTag(s_stFuncTable, "player");
	}
	return s_stFuncTable;
}

ExprContext *zmapGetExprContext(void)
{
	static ExprContext *s_pExprContext = NULL;

	if (!s_pExprContext)
	{
		s_pExprContext = exprContextCreate();
		exprContextSetFuncTable(s_pExprContext, zmapCreateExprFuncTable());
		exprContextSetAllowRuntimeSelfPtr(s_pExprContext);
		exprContextSetAllowRuntimePartition(s_pExprContext);
	}

	return s_pExprContext;
}

static void zmapGenerateExpression(Expression *expr, const char *pchReason, const char *pchFilename)
{
	if (expr && !exprGenerate(expr, zmapGetExprContext()))
	{
		ErrorFilenamef(pchFilename, "Failed to generate zmap expression %s.", pchReason);
	}
}

static void zmapInfoGenerate(ZoneMapInfo *pZoneInfo)
{
	zmapGenerateExpression(pZoneInfo->requires_expr, "RequiresExpr", zmapInfoGetFilename(pZoneInfo));
	zmapGenerateExpression(pZoneInfo->permission_expr, "PermissionExpr", zmapInfoGetFilename(pZoneInfo));
}

static void zmapInfoValidate(ZoneMapInfo *pZoneInfo)
{
	int i;

	// Stuff to do even if map is "private_to" because the ZoneMapInfo is still packed up
	// and shipped to clients, so all references need to be valid.  Validate any actual references out here.

	// Validate messages
	if (IsGameServerSpecificallly_NotRelatedTypes() && REF_STRING_FROM_HANDLE( pZoneInfo->display_name.hMessage )) {
		if( !GET_REF( pZoneInfo->display_name.hMessage )) {
			ErrorFilenamef( pZoneInfo->filename, "Zonemap: %s -- Zonemap refers to non-existent message '%s'",
							pZoneInfo->map_name, REF_STRING_FROM_HANDLE( pZoneInfo->display_name.hMessage ));
		} else if( !strStartsWith( GET_REF( pZoneInfo->display_name.hMessage )->pcFilename,
								   pZoneInfo->filename )) {
			ErrorFilenamef( pZoneInfo->filename, "Zonemap: %s -- Zonemap refers to message '%s' that it does not own.  This requires textfile hacking to fix.",
							pZoneInfo->map_name, REF_STRING_FROM_HANDLE( pZoneInfo->display_name.hMessage ));
		}
	}

	// Stuff to do only if the map isn't "private_to".

	// If the map is private, we don't care about validating it on the CB.
	if (eaSize(&pZoneInfo->private_to) && g_isContinuousBuilder)
		return;
	
	if ((pZoneInfo->map_type == ZMTYPE_UNSPECIFIED) && !eaSize(&pZoneInfo->private_to)) {
		ErrorFilenamef(pZoneInfo->filename, "Zonemap: %s -- ZoneMap is type UNSPECIFIED and is not private.  Only private maps may be UNSPECIFIED.  All non-private maps are sent to customers and must have a specified type in order to operate.",
					   pZoneInfo->map_name);
	}

	if (!eaSize(&pZoneInfo->private_to) && pZoneInfo->genesis_data)
	{
		ErrorFilenamef(pZoneInfo->filename, "Zonemap: %s -- ZoneMap has Genesis data and is not private. This MUST NOT happen in a final production build! All Genesis maps must be committed to layers before release.", pZoneInfo->map_name);
	}

	// Validate that the name and filename match in terms of namespace
	{
		char pcNameNS[1024], pcFileNS[1024], pcBase[1024];
		resExtractNameSpace(pZoneInfo->map_name, pcNameNS, pcBase);
		resExtractNameSpace(pZoneInfo->filename, pcFileNS, pcBase);
		if (stricmp( pcFileNS, pcNameNS ) != 0) {
			ErrorFilenamef( pZoneInfo->filename, "Zonemap: %s -- Zonemap name %s does not match file namespace",
							pZoneInfo->filename, pZoneInfo->map_name );
		}
	}

	// Validate that regions are legal types
	for(i=eaSize(&pZoneInfo->regions)-1; i>=0; --i) {
		WorldRegion *pRegion = pZoneInfo->regions[i];
		if (!isValidRegionTypeForGame(pRegion->type)) {
			ErrorFilenamef( pZoneInfo->filename, "Zonemap: %s -- Zonemap type %s is not supported for this game",
							pZoneInfo->filename, StaticDefineIntRevLookup(WorldRegionTypeEnum, pRegion->type) );
		}
	}

	// Validate that if this map has a ugcinfo, it's marked as "UsedInUGC"
	{
		char ugcFilename[ MAX_PATH ];
		changeFileExt( pZoneInfo->filename, ".ugcinfo", ugcFilename );
		if( fileExists( ugcFilename ) && zmapInfoGetUsedInUGC( pZoneInfo ) != ZMAP_UGC_USED_AS_EXTERNAL_MAP ) {
			ErrorFilenamef( pZoneInfo->filename, "Zonemap: %s -- Zonemap has a .ugcinfo file, but is not marked as \"UGC Used As External Map\".",
							pZoneInfo->filename );
		}
	}

	// Validate that the zone map's filename matches the zone map's
	// name.  This prevents putting two zone maps in one file, which
	// confuses the binning process.
	{
		char* lastSlash = strrchr( pZoneInfo->filename, '/' );
		char zoneMapFilenameSansDirectoryAndExt[ MAX_PATH ];
		char zoneMapNameSansNamespace[ RESOURCE_NAME_MAX_SIZE ];

		if( !lastSlash ) {
			strcpy( zoneMapFilenameSansDirectoryAndExt, pZoneInfo->filename );
		} else {
			strcpy( zoneMapFilenameSansDirectoryAndExt, lastSlash + 1 );
		}
		changeFileExt( zoneMapFilenameSansDirectoryAndExt, "", zoneMapFilenameSansDirectoryAndExt );
		resExtractNameSpace_s( pZoneInfo->map_name, NULL, 0, SAFESTR( zoneMapNameSansNamespace ));

		if( stricmp( zoneMapFilenameSansDirectoryAndExt, zoneMapNameSansNamespace ) != 0 ) {
			ErrorFilenamef( pZoneInfo->filename, "Zonemap: %s -- Zonemap filename does not match the zone map name.  This map may not bin properly.", pZoneInfo->map_name );
		}
	}
}

static int zmapInfoValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ZoneMapInfo *pZoneInfo, U32 userID)
{
	switch (eType)
	{
		xcase RESVALIDATE_POST_TEXT_READING:
		{
			zmapInfoGenerate(pZoneInfo);
			zmapInfoValidate(pZoneInfo);
		}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static bool g_ZeniLoadOnlyUGC = false;

static int zeniValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, ZoneMapEncounterInfo *pZeniInfo, U32 userID)
{
	switch( eType )
	{
		xcase RESVALIDATE_POST_TEXT_READING:
			if( g_ZeniLoadOnlyUGC && pZeniInfo ) {
				ZoneMapInfo* zmapInfo = RefSystem_ReferentFromString( g_ZoneMapDictionary, pResourceName );

				if(   zmapInfo && zmapInfoGetUsedInUGC( zmapInfo ) != ZMAP_UGC_USED_AS_ASSET
					  && !pZeniInfo->deprecated_map_new_map_name ) {
					int it;
					for( it = eaSize( &pZeniInfo->objects ) - 1; it >= 0; --it ) {
						ZoneMapEncounterObjectInfo* pZeniObj = pZeniInfo->objects[ it ];
						if( !zeniObjIsUGCData( pZeniObj )) {
							StructDestroy( parse_ZoneMapEncounterObjectInfo, pZeniObj );
							eaRemove( &pZeniInfo->objects, it );
						}
					}

					if( eaSize( &pZeniInfo->objects ) == 0 ) {
						resDoNotLoadCurrentResource();
					}
				}

				if( zmapInfo && zmapInfoGetUsedInUGC( zmapInfo ) == ZMAP_UGC_USED_AS_ASSET ) {
					bool foundStartSpawnAccum = false;
					FOR_EACH_IN_EARRAY_FORWARDS( pZeniInfo->objects, ZoneMapEncounterObjectInfo, pZeniObj ) {
						if( stricmp( pZeniObj->logicalName, "UGC_Start_Spawn" ) == 0 ) {
							foundStartSpawnAccum = true;
						}
					} FOR_EACH_END;

					if( !foundStartSpawnAccum ) {
						Errorf( "UGC tagged ZoneMap %s does not have a UGC_Start_Spawn.", pResourceName );
					}
				}
			}
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

AUTO_RUN;
int RegisterZoneMapDictionary(void)
{
	g_ZoneMapDictionary = RefSystem_RegisterSelfDefiningDictionary("ZoneMap", false, parse_ZoneMapInfo, true, true, NULL);
	g_ZoneMapEncounterInfoDictionary = RefSystem_RegisterSelfDefiningDictionary("ZoneMapEncounterInfo", false, parse_ZoneMapEncounterInfo, true, true, NULL);
	g_ZoneMapExternalMapSnapDictionary = RefSystem_RegisterSelfDefiningDictionary("ZoneMapExternalMapSnap", false, parse_ZoneMapExternalMapSnap, true, true, NULL);

	if (IsServer())
	{
		resDictProvideMissingResources(g_ZoneMapDictionary);
		if (isDevelopmentMode() || isProductionEditMode())
		{
			resDictMaintainInfoIndex(g_ZoneMapDictionary, ".MapName", NULL, ".Tags", NULL, NULL);
		}
		resDictManageValidation(g_ZoneMapDictionary, zmapInfoValidateCB);

		resDictGetMissingResourceFromResourceDBIfPossible((void*)g_ZoneMapDictionary);

		if( IsGameServerSpecificallly_NotRelatedTypes() ) {
			resDictProvideMissingResources(g_ZoneMapEncounterInfoDictionary);
			resDictProvideMissingResources(g_ZoneMapExternalMapSnapDictionary);
		}
	}
	else if (IsClient())
	{
		resDictRequestMissingResources(g_ZoneMapDictionary, 8, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_ZoneMapEncounterInfoDictionary, 8, false, resClientRequestSendReferentCommand);
	}
	
	resDictManageValidation(g_ZoneMapEncounterInfoDictionary, zeniValidateCB);
	resDictManageValidation(g_ZoneMapExternalMapSnapDictionary, NULL);

	return 1;
}

AUTO_FIXUPFUNC;
TextParserResult zmapFixup(ZoneMapInfo *zminfo, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
#ifndef NO_EDITORS
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			// Make sure there is a public name
			if (!zminfo->map_name || !zminfo->map_name[0])
			{
				char name[256];
				getFileNameNoExt(name, zminfo->filename);
				zminfo->map_name = allocAddString(name);
			}
			assert(zminfo->map_name);

			// Resource DB needs to strip data loaded from patched text
			if (GetAppGlobalType() == GLOBALTYPE_RESOURCEDB)
				g_StripGenesisData = true;

			if (zminfo->genesis_data) {
				// Make sure genesis has ONLY the expected map variables
				genesisFixupZoneMapInfo(zminfo);
				genesisInternVariableDefs(zminfo, true);

				// Make sure that maps get the tracking flags
				zminfo->disable_visited_tracking = !zminfo->genesis_data->is_map_tracking_enabled;			
			}

			if (g_StripGenesisData)
			{
				StructDestroySafe(parse_GenesisZoneMapData, &zminfo->backup_genesis_data);
				StructDestroySafe(parse_GenesisZoneMapData, &zminfo->genesis_data);
			}
			if (g_StripExtraData)
			{
				int i, j;

				StructDestroySafe(parse_GenesisZoneMapInfo, &zminfo->genesis_info);
				eaDestroyStruct(&zminfo->variable_defs, parse_WorldVariableDef);
				REMOVE_HANDLE(zminfo->reward_table);
				REMOVE_HANDLE(zminfo->player_reward_table);

				for(i=eaSize(&zminfo->regions)-1; i>=0; --i) {
					WorldRegion *pRegion = zminfo->regions[i];
					for(j=eaSize(&pRegion->pRegionRulesOverride.ppTempPuppets)-1; j>=0; --j) {
						TempPuppetChoice *pTemp = pRegion->pRegionRulesOverride.ppTempPuppets[j];
						REMOVE_HANDLE(pTemp->hPetDef);
					}
				}
			}
		}
#endif
	}
	return 1;
}

TextParserResult fixupZoneMapEncounterInfo(ZoneMapEncounterInfo* zeni, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			char zoneFileName[ CRYPTIC_MAX_PATH ];
			TextParserState *tps = (TextParserState*)pExtraData;

			// We don't care about namespace'd ZENIs, those are never
			// referenced by UGC.
			if( !strStartsWith( zeni->filename, "ns/" )) {
				assert( strStartsWith( zeni->filename, "tempbin/" ));
				strcpy( zoneFileName, zeni->filename + strlen( "tempbin/" ));

				assert( strEndsWith( zoneFileName, ".zeni" ));
				{
					size_t pos = strlen( zoneFileName ) - strlen( ".zeni" );
					zoneFileName[ pos ] = '\0';
				}
				ParserBinAddFileDep( tps, zoneFileName );
			}
		}
	}
	return 1;
}

TextParserResult fixupZoneMapExternalMapSnap(ZoneMapExternalMapSnap* zeniSnap, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		xcase FIXUPTYPE_POST_TEXT_READ:
		{
			// The client never reads the path nodes, so we can clear them on the client
			if( IsClient() ) {
				eaDestroyStruct( &zeniSnap->eaPathNodes, parse_ZoneMapMetadataPathNode );
			}
		}
	}
	return 1;
}

void zmapManualFixup(ZoneMapInfo *zminfo)
{
	zmapFixup(zminfo, FIXUPTYPE_POST_TEXT_READ, NULL);
}

void zmapResDictEventCallback(enumResourceEventType eType, const char *pDictName, const char *pResourceName, void *pResource, void *pUserData)
{
	static U32 prevTimestamp = 0;
	static const char *prevName = NULL;
	ZoneMapInfo *zminfo = (ZoneMapInfo*) pResource;

	if (!zminfo || isProductionEditMode())
		return;

	// deal with reloading on the server
	if (wlIsServer())
	{
		if (eType == RESEVENT_RESOURCE_LOCKED || eType == RESEVENT_RESOURCE_UNLOCKED || eType == RESEVENT_RESOURCE_MODIFIED || eType == RESEVENT_RESOURCE_ADDED)
		{
			int i;
			bool need_reload = false;

			// now we check whether the event was triggered for the current map
			for (i = 0; i < eaSize(&world_grid.maps); i++)
			{
				if (world_grid.maps[i]->map_info.filename == zminfo->filename)
				{
					ZoneMapInfo *loaded_map = world_grid.maps[i]->last_saved_info;

					// if saving, copy the newly saved info to the active map for diff'ing later
					if (zminfo->saving)
					{
						StructCopy(parse_ZoneMapInfo, zminfo, loaded_map, 0, 0, 0);
						break;
					}
					// if no difference is found between the last saved/loaded map and the newly modified one, don't do anything
					else if (StructCompare(parse_ZoneMapInfo, loaded_map, zminfo, 0, 0, TOK_NO_TEXT_SAVE | TOK_NO_WRITE | TOK_USEROPTIONBIT_1) == 0)
						break;

					// reload will be necessary
					need_reload = true;
				}
			}

			if (!zminfo->saving && need_reload && !world_grid.deferred_load_map)
			{
				// queue up a reload
				if (world_grid.deferred_load_map)
					SAFE_FREE(world_grid.deferred_load_map);
				world_grid.deferred_load_map = strdup(world_grid.maps[0]->map_info.filename);
			}

			if (zminfo->saving)
				zminfo->saving = false;
		}
	}
}

int zmapLoadDictionary(void)
{
	static int loadedOnce = false;

	if (!loadedOnce)
	{
		loadedOnce = true;

		if (IsClient()) 
		{
			// Client always loads the client version at first
			g_StripGenesisData = true;
			g_StripExtraData = true;
			resLoadResourcesFromDisk(g_ZoneMapDictionary, "maps", ".zone", "zonemaps_client.bin", RESOURCELOAD_USERDATA);

			if (gbMakeBinsAndExit)
			{
				// If making bins, then also need the full version of the data loaded after creating client version
				g_StripGenesisData = false;
				g_StripExtraData = false;
				RefSystem_ClearDictionary(g_ZoneMapDictionary, true);
				resLoadResourcesFromDisk(g_ZoneMapDictionary, "maps", ".zone", "zonemaps_dev.bin", PARSER_BINS_ARE_SHARED | RESOURCELOAD_USERDATA);
			}
		}
		else
		{
			// Don't load regular data first if on map manager, but all other m
			if (!IsMapManager())
			{
				if (isDevelopmentMode()) 
				{
					g_StripGenesisData = false;
					g_StripExtraData = false;
					resLoadResourcesFromDisk(g_ZoneMapDictionary, "maps", ".zone", "zonemaps_dev.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_BINS_ARE_SHARED | RESOURCELOAD_USERDATA);
				}
				if (!isDevelopmentMode() || gbMakeBinsAndExit)
				{
					g_StripGenesisData = true;
					g_StripExtraData = false;
					RefSystem_ClearDictionary(g_ZoneMapDictionary, true);
					resLoadResourcesFromDisk(g_ZoneMapDictionary, "maps", ".zone", "zonemaps_server.bin", RESOURCELOAD_SHAREDMEMORY | RESOURCELOAD_USERDATA);
				}
			}

			if (IsMapManager() || (gbMakeBinsAndExit && isDevelopmentMode()))
			{
				// This bin file is generated by GameServer during makebins but only should be loaded by the MapManager at runtime
				g_StripGenesisData = true;
				g_StripExtraData = false;
				RefSystem_ClearDictionary(g_ZoneMapDictionary, true);
				resLoadResourcesFromDisk(g_ZoneMapDictionary, "maps", ".zone", "zonemaps_allns.bin", RESOURCELOAD_ALLNS);
			}
		}

		{
			// There has been a lot of issues with the
			// ZoneMapEncounterInfo dictionary putting resources into
			// shared memory even though the dictionary is in edit
			// mode.  This serves as a quick patch to avoid that
			// issue.
			int flags = PARSER_OPTIONALFLAG;
			if( isProductionMode() ) {
				flags |= RESOURCELOAD_SHAREDMEMORY;
			}

			// Client loads through zmapLoadClientDictionary
			if( gbMakeBinsAndExit || g_isContinuousBuilder || isProductionEditMode() || isProductionMode() ) {
				if( !wlIsClient()) {
					resLoadResourcesFromDisk(g_ZoneMapEncounterInfoDictionary, "tempbin", ".zeni", NULL, flags );
					if( isProductionEditAvailable() ) {
						resLoadResourcesFromDisk(g_ZoneMapExternalMapSnapDictionary, "tempbin", ".zeni_snap", NULL, flags );
					}
				} else {
					zmapLoadClientDictionary();
				}
			}
		}

		resDictRegisterEventCallback(g_ZoneMapDictionary, zmapResDictEventCallback, NULL);
	}

	// Clear flags before finish loading so that reloads don't have stripping behavior
	g_StripGenesisData = false;
	g_StripExtraData = false;

	return 1;
}

void zmapLoadClientDictionary(void)
{
	static bool bLoaded = false;

	if (bLoaded) {
		return;
	}
	bLoaded = true;

	if (IsClient()) {		
		g_ZeniLoadOnlyUGC = true;
		resLoadResourcesFromDisk(g_ZoneMapEncounterInfoDictionary, "tempbin", ".zeni", "ZonemapEncounterInfo_UGC.bin", PARSER_OPTIONALFLAG | PARSER_BINS_ARE_SHARED );
		g_ZeniLoadOnlyUGC = false;

		if( isProductionEditAvailable() ) {
			resLoadResourcesFromDisk(g_ZoneMapExternalMapSnapDictionary, "tempbin", ".zeni_snap", NULL, PARSER_OPTIONALFLAG );
		}
	}
}

/// Do not call this outside of make bins and exit!!!!!!
void zmapForceBinEncounterInfo(void)
{
	assert(gbMakeBinsAndExit);
	
	// Reload encounterinfo bin, so we can ensure it gets created in one pass			
	if( !wlIsClient() ) {
		RefSystem_ClearDictionary(g_ZoneMapEncounterInfoDictionary,false);
		resLoadResourcesFromDisk(g_ZoneMapEncounterInfoDictionary, "tempbin", ".zeni", NULL, PARSER_OPTIONALFLAG );
	}
	if( wlIsClient() ) {
		RefSystem_ClearDictionary(g_ZoneMapEncounterInfoDictionary,false);
		g_ZeniLoadOnlyUGC = true;
		resLoadResourcesFromDisk(g_ZoneMapEncounterInfoDictionary, "tempbin", ".zeni", "ZonemapEncounterInfo_UGC.bin", PARSER_OPTIONALFLAG );
		g_ZeniLoadOnlyUGC = false;
	
		RefSystem_ClearDictionary(g_ZoneMapExternalMapSnapDictionary,false);
		if( isProductionEditAvailable() ) {
			resLoadResourcesFromDisk(g_ZoneMapExternalMapSnapDictionary, "tempbin", ".zeni_snap", NULL, PARSER_OPTIONALFLAG );
		}
	}
}

//////////////////////////////////////////////////////////////////
// ZoneMapInfo Resource Functions
//////////////////////////////////////////////////////////////////

ZoneMapInfo *zmapInfoGetByPublicName(const char *name)
{
	return RefSystem_ReferentFromString(g_ZoneMapDictionary, name);
}

void zmapInfoSetUpdateCallback(resCallback_HandleEvent evt, void *userdata)
{
	resDictRegisterEventCallback(g_ZoneMapDictionary, evt, userdata);
}

void zmapInfoRemoveUpdateCallback(resCallback_HandleEvent evt)
{
	resDictRemoveEventCallback(g_ZoneMapDictionary, evt);
}

//////////////////////////////////////////////////////////////////
// ZoneMapInfo validators
//////////////////////////////////////////////////////////////////

#define VALIDATE_ZMINFO(zminfo) if (!(zminfo)) (zminfo) = world_grid.active_map ? &world_grid.active_map->map_info : NULL; if (!(zminfo)) return;
#define VALIDATE_ZMINFO_RET(zminfo, ret) if (!(zminfo)) (zminfo) = world_grid.active_map ? &world_grid.active_map->map_info : NULL; if (!(zminfo)) return (ret);

//////////////////////////////////////////////////////////////////
// ZoneMapInfo getters/setters
//////////////////////////////////////////////////////////////////

bool zmapInfoIsTestMap(const ZoneMapInfo *zminfo)
{
	return !!strstri(zmapInfoGetFilename(zminfo), "Maps/TestMaps/");
}

bool zmapInfoIsAvailable(const ZoneMapInfo *zminfo, bool allow_private_maps)
{
	const char *username;
	int i;
	VALIDATE_ZMINFO_RET(zminfo, false);

	if (eaSize(&zminfo->private_to) == 0)
		return true;

	if (g_isContinuousBuilder || isProductionMode())
		return false; // no private maps allowed in production mode

	if (wl_state.allow_all_private_maps)
		return true;

	if (!allow_private_maps)
		return false; // check this AFTER wl_state.allow_all_private_maps so that if gimme goes down people can still load private maps

	username = gimmeDLLQueryUserName();
	for (i = 0; i < eaSize(&zminfo->private_to); ++i)
	{
		if (stricmp(zminfo->private_to[i], "All")==0)
			return true;
		if (username && stricmp(username, zminfo->private_to[i])==0)
			return true;
		if (!g_disableLastAuthor && UserIsInGroup(zminfo->private_to[i]))
			return true;
		if (!g_disableLastAuthor && wl_state.allow_group_private_maps && IsGroupName(zminfo->private_to[i]))
			return true;
	}

	return false;
}

AUTO_TRANS_HELPER_SIMPLE;
const char *zmapInfoGetPublicName(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->map_name;
}

const char *zmapInfoGetCurrentName(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->new_map_name ? zminfo->new_map_name : zminfo->map_name;
}

const char *zmapInfoGetFilename(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->filename;
}

void zmapInfoSetFilenameForDemo( ZoneMapInfo *zminfo, const char* filename)
{
	zminfo->filename = allocAddFilename( filename );
}

ZoneMapType zmapInfoGetMapType(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, ZMTYPE_STATIC);
	return zminfo->map_type;
}

const char *zmapInfoGetDefaultQueueDef(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->default_queue;
}

const char *zmapInfoGetDefaultPVPGameType(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->default_gametype;
}

ZoneMapType zmapInfoGetMapTypeByName(const char *mapName)
{
	ZoneMapInfo *zmapInfo = zmapInfoGetByPublicName(mapName);
	if ( zmapInfo != NULL )
	{
		return zmapInfoGetMapType(zmapInfo);
	}

	return ZMTYPE_UNSPECIFIED;
}

ZoneRespawnType zmapInfoGetRespawnType(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, ZMTYPE_STATIC);
	return zminfo->eRespawnType;
}

U32 zmapInfoGetRespawnWaveTime(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, ZMTYPE_STATIC);

	return zminfo->RespawnWaveTime;
}

U32 zmapInfoGetMapLevel(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->level;
}

S32 zmapInfoGetMapDifficulty(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->difficulty;
}

U32 zmapInfoGetMapForceTeamSize(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->force_team_size;
}

bool zmapInfoGetMapIgnoreTeamSizeBonusXP(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->bIgnoreTeamSizeBonusXP;
}

U32 zmapInfoGetNotPlayerVisited(const ZoneMapInfo* zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->not_player_visited;
}

U32 zmapInfoGetNoBeacons(const ZoneMapInfo* zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->no_beacons;
}

F32 zmapInfoGetMapSnapOutdoorRes(const ZoneMapInfo* zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->mapSnapOutdoorRes;
}

F32 zmapInfoGetMapSnapIndoorRes(const ZoneMapInfo* zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->mapSnapIndoorRes;
}

const char **zmapInfoGetPrivacy(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->private_to;
}

int zmapInfoGetLayerCount(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return eaSize(&zminfo->layers);
}

const char *zmapInfoGetLayerPath(const ZoneMapInfo *zminfo, int i)
{
	char zmap_path[MAX_PATH], *s;
	ZoneMapLayerInfo *layer;
	VALIDATE_ZMINFO_RET(zminfo, NULL);

	strcpy(zmap_path, zminfo->filename);
	s = strrchr(zmap_path, '/');
	if (s)
		*(s+1) = 0;

	if (i < 0 || i > eaSize(&zminfo->layers))
		return NULL;

	assert(zminfo->layers);
	layer = zminfo->layers[i];
	assert(layer);

	// make relative layer paths absolute
	if (   layer->filename 
		&& !strStartsWith(layer->filename, "maps/")
		&& !strStartsWith(layer->filename, "ns/"))
	{
		char layer_filename[MAX_PATH];
		sprintf(layer_filename, "%s%s", zmap_path, layer->filename);
		return allocAddFilename(layer_filename);
	}

	return layer->filename;
}

ZoneMapTimeBlock **zmapInfoGetTimeBlocks(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->time_blocks;
}

// TomY ENCOUNTER_HACK
bool zmapInfoAllowEncounterHack(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return !!zminfo->allow_encounter_hack;
}

bool zmapInfoConfirmPurchasesOnExit(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return !!zminfo->confirm_purchases_on_exit;
}

bool zmapInfoGetCollectDoorDestStatus(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->collect_door_dest_status;
}

bool zmapInfoGetDisableDuels(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->disable_duels;
}

bool zmapInfoGetPowersRequireValidTarget(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->powers_require_valid_target;
}

bool zmapInfoGetEnableShardVariables(const ZoneMapInfo *zminfo)
{
	//web request server always has all shard variables
	if (GetAppGlobalType() == GLOBALTYPE_WEBREQUESTSERVER || GetAppGlobalType() == GLOBALTYPE_GATEWAYSERVER)
	{
		return true;
	}

	// We used to test gConf.bFCSpecialPublicShardVariablesFeature here. This has been deprecated as we now allow
	//  BOTH types of variables, broadcast and map requested. Broadcast is expensive.
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->enable_shard_variables;
}

bool zmapInfoGetDisableInstanceChanging(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->disable_instance_change;
}

bool zmapInfoGetTeamNotRequired(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->team_not_required;
}

bool zmapInfoGetGuildNotRequired(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->guild_not_required;
}

bool zmapInfoGetIsGuildOwned(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->guild_owned;
}

bool zmapInfoGetTerrainStaticLighting(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->terrain_static_lighting;
}

bool zmapInfoGetDisableVisitedTracking(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return zminfo->disable_visited_tracking;
}

bool zmapInfoGetEffectiveDisableVisitedTracking(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);

	// Disabled if has namespace and we disallow tracking for namespace maps
	if (!gConf.bAllowVisitedTrackingForNamespaceMaps && resHasNamespace(zminfo->map_name)) {
		return true;
	}

	// Disabled if manually set by artist/designer
	if (zminfo->disable_visited_tracking) {
		return true;
	}

	// Disabled if map type is one of these
	if ((zminfo->map_type == ZMTYPE_OWNED) || 
		(zminfo->map_type == ZMTYPE_PVP) ||
		(zminfo->map_type == ZMTYPE_QUEUED_PVE)) {
		return true;
	}

	// Otherwise visited tracking is enabled
	return false;
}

F32	zmapInfoGetWindLargeObjectRadiusThreshold(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->wind_large_object_radius_threshold;
}


const char*	zmapInfoGetParentMapName(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	if(zminfo->pParentMap) {
		return zminfo->pParentMap->pchMapName;
	}
	return NULL;
}

const char*	zmapInfoGetParentMapSpawnPoint(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	if(zminfo->pParentMap) {
		return zminfo->pParentMap->pchSpawnPoint;
	}
	return NULL;
}

const char* zmapInfoGetStartSpawnName(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->start_spawn_name;
}

bool zmapInfoGetRecordPlayerMatchStats(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->record_player_match_stats;
}

bool zmapInfoGetEnableUpsellFeatures(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return !!zminfo->enable_upsell_features;
}

bool zmapInfoGetRespawnTimes(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo, U32 *min, U32 *max, U32 *increment, U32 *attrition)
{
	VALIDATE_ZMINFO_RET(zminfo, false);

	if (zminfo->respawn_data)
	{
		*min = zminfo->respawn_data->min_time;
		*max = zminfo->respawn_data->max_time;
		*increment = zminfo->respawn_data->increment;
		*attrition = zminfo->respawn_data->attrition_time;
		return true;
	}
	else
	{
		*min = 0;
		*max = 0;
		*increment = 0;
		*attrition = 0;
		return false;
	}
}

ZoneMapUGCUsage zmapInfoGetUsedInUGC(const ZoneMapInfo* zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);

	if( zminfo->eUsedInUGC ) {
		return zminfo->eUsedInUGC;
	}

	// Legacy support: Check for the "UGC" tag
	if( zminfo->deprecated_tags && strstri( zminfo->deprecated_tags, "UGC" )) {
		return ZMAP_UGC_USED_AS_ASSET;
	}
	
	return ZMAP_UGC_UNUSED;
}

// Fills in a fake ZoneMapInfo from a ZoneMapInfoRequest
// This function performs pointer copies of data in zminfoRequest and clears all fields in zminfoRequest
ZoneMapInfo* zmapInfoGetFromRequest(SA_PARAM_NN_VALID ZoneMapInfoRequest *zminfoRequest)
{
	static ZoneMapInfo s_zminfo = {0};

	s_zminfo.map_type = zminfoRequest->eMapType;
	s_zminfo.confirm_purchases_on_exit = zminfoRequest->bConfirmPurchasesOnExit;
	
	// Display Name
	SET_HANDLE_FROM_STRING("Message", zminfoRequest->pchDisplayNameMsgKey, s_zminfo.display_name.hMessage);
	StructFreeString(zminfoRequest->pchDisplayNameMsgKey);

	// GenesisZoneMapData
	if (s_zminfo.genesis_data) {
		StructDestroy(parse_GenesisZoneMapData, s_zminfo.genesis_data);
	}
	s_zminfo.genesis_data = zminfoRequest->pGenesisData;

	// GenesisZoneMapInfo
	if (s_zminfo.genesis_info) {
		StructDestroy(parse_GenesisZoneMapInfo, s_zminfo.genesis_info);
	}
	s_zminfo.genesis_info = zminfoRequest->pGenesisInfo;
	
	// WorldRegions
	if (s_zminfo.regions) {
		eaDestroyStruct(&s_zminfo.regions, parse_WorldRegion);
	}
	s_zminfo.regions = zminfoRequest->eaRegions;
	
	// VariableDefs
	if (s_zminfo.variable_defs) {
		eaDestroyStruct(&s_zminfo.variable_defs, parse_WorldVariableDef);
	}
	s_zminfo.variable_defs = zminfoRequest->eaVarDefs;
	
	// Requires Expression
	s_zminfo.requires_expr = zminfoRequest->pRequiresExpr;
	zmapGenerateExpression(s_zminfo.requires_expr, "RequiresExpr from request", zmapInfoGetFilename(&s_zminfo));
	s_zminfo.permission_expr = zminfoRequest->pPermissionExpr;
	zmapGenerateExpression(s_zminfo.permission_expr, "PermissionExpr from request", zmapInfoGetFilename(&s_zminfo));

	// Clear all fields in the request structure
	ZeroStruct(zminfoRequest);
	return &s_zminfo;
}

ContainerID zmapInfoGetUGCProjectID(SA_PARAM_OP_VALID const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->ugcProjectID;
}

//////////////////////////////////////////////////////////////////
// ZoneMapInfo Display Names
//////////////////////////////////////////////////////////////////

const char *zmapInfoGetDefaultDisplayNameMsgKey(const ZoneMapInfo *zminfo)
{
	const char *zmapname;
	VALIDATE_ZMINFO_RET(zminfo, NULL);

	zmapname = zmapInfoGetPublicName(zminfo);
	if (zmapname)
	{
		char key[MAX_PATH];

		strcpy(key, zmapname);
		strchrReplace(key, '.', '_');
		strchrReplace(key, '/', '_');
		strchrReplace(key, '\\', '_');
		strcat(key, ".DisplayName");
		return allocAddString(key);
	}

	return NULL;
}

const char *zmapInfoGetDisplayNameMsgKey(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return REF_STRING_FROM_HANDLE(zminfo->display_name.hMessage);
}

DisplayMessage *zmapInfoGetDisplayNameMessage(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return &zminfo->display_name;
}

Message *zmapInfoGetDisplayNameMessagePtr(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return GET_REF(zminfo->display_name.hMessage);
}

//////////////////////////////////////////////////////////////////
// Reward Table Data
//////////////////////////////////////////////////////////////////

const char *zmapInfoGetRewardTableString(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return REF_STRING_FROM_HANDLE(zminfo->reward_table);
}

RewardTable *zmapInfoGetRewardTable(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return GET_REF(zminfo->reward_table);
}

void zmapInfoSetRewardTable(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *rewardTableKey)
{
	VALIDATE_ZMINFO(zminfo);
	if(IS_HANDLE_ACTIVE(zminfo->reward_table))
		REMOVE_HANDLE(zminfo->reward_table);
	if(!rewardTableKey)
		return;
	SET_HANDLE_FROM_STRING("RewardTable", rewardTableKey, zminfo->reward_table);
	zminfo->mod_time++;
}

const char *zmapInfoGetPlayerRewardTableString(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return REF_STRING_FROM_HANDLE(zminfo->player_reward_table);
}

RewardTable *zmapInfoGetPlayerRewardTable(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return GET_REF(zminfo->player_reward_table);
}

void zmapInfoSetPlayerRewardTable(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *rewardTableKey)
{
	VALIDATE_ZMINFO(zminfo);
	if(IS_HANDLE_ACTIVE(zminfo->player_reward_table))
		REMOVE_HANDLE(zminfo->player_reward_table);
	if(!rewardTableKey)
		return;
	SET_HANDLE_FROM_STRING("RewardTable", rewardTableKey, zminfo->player_reward_table);
	zminfo->mod_time++;
}

//////////////////////////////////////////////////////////////////
// Expressions
//////////////////////////////////////////////////////////////////

Expression *zmapInfoGetRequiresExpr(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->requires_expr;
}

void zmapInfoSetRequiresExpr(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, Expression *expr)
{
	VALIDATE_ZMINFO(zminfo);
	exprDestroy(zminfo->requires_expr);
	zminfo->requires_expr = exprClone(expr);
	zminfo->mod_time++;
}

Expression *zmapInfoGetPermissionExpr(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->permission_expr;
}

void zmapInfoSetPermissionExpr(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, Expression *expr)
{
	VALIDATE_ZMINFO(zminfo);
	exprDestroy(zminfo->permission_expr);
	zminfo->permission_expr = exprClone(expr);
	zminfo->mod_time++;
}

//////////////////////////////////////////////////////////////////
// Required Class Category Set
//////////////////////////////////////////////////////////////////

const char *zmapInfoGetRequiredClassCategorySetString(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return REF_STRING_FROM_HANDLE(zminfo->required_class_category_set);
}

CharClassCategorySet *zmapInfoGetRequiredClassCategorySet(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return GET_REF(zminfo->required_class_category_set);
}

void zmapInfoSetRequiredClassCategorySet(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char* categorySet)
{
	VALIDATE_ZMINFO(zminfo);
	if(IS_HANDLE_ACTIVE(zminfo->required_class_category_set))
		REMOVE_HANDLE(zminfo->required_class_category_set);
	if(!categorySet)
		return;
	SET_HANDLE_FROM_STRING("CharClassCategorySet", categorySet, zminfo->required_class_category_set);
	zminfo->mod_time++;
}

//////////////////////////////////////////////////////////////////
// Mastermind Data
//////////////////////////////////////////////////////////////////

const char *zmapInfoGetMastermindDefKey(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->mastermind_def;
}


void zmapInfoSetMastermindDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *mastermindDefKey)
{
	VALIDATE_ZMINFO(zminfo);
	if (mastermindDefKey)
		zminfo->mastermind_def = allocAddString(mastermindDefKey);
	else 
		zminfo->mastermind_def = NULL;

	if (wl_state.mastermind_def_updated_callback)
		wl_state.mastermind_def_updated_callback(zminfo->mastermind_def);

	zminfo->mod_time++;
}


//////////////////////////////////////////////////////////////////
// Civilian Map Def
//////////////////////////////////////////////////////////////////

const char *zmapInfoGetCivilianMapDefKey(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->civilian_def;
}

void zmapInfoSetCivilianMapDef(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *civilianDefKey)
{
	VALIDATE_ZMINFO(zminfo);
	if (civilianDefKey)
		zminfo->civilian_def = allocAddString(civilianDefKey);
	else 
		zminfo->civilian_def = NULL;

	//if (wl_state.mastermind_def_updated_callback)
	//	wl_state.mastermind_def_updated_callback(zminfo->mastermind_def);

	zminfo->mod_time++;
}


//////////////////////////////////////////////////////////////////
// PlayerFSMs
//////////////////////////////////////////////////////////////////

const char **zmapInfoGetPlayerFSMs(SA_PARAM_OP_VALID ZoneMapInfo* zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);

	return zminfo->playerFSMs;
}


//////////////////////////////////////////////////////////////////
// ZoneMapInfo Genesis Data
//////////////////////////////////////////////////////////////////

bool zmapInfoHasGenesisData(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return !!zminfo->genesis_data;
}

bool zmapInfoHasBackupGenesisData(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return !!zminfo->backup_genesis_data;
}

GenesisZoneMapData *zmapInfoGetGenesisData(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->genesis_data;
}

GenesisZoneMapInfo *zmapInfoGetGenesisInfo(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return zminfo->genesis_info;
}

GenesisMapDescription *zmapInfoGetMapDesc(ZoneMapInfo *zminfo)
{
	GenesisZoneMapData *genesis_data;
	genesis_data = zmapInfoGetGenesisData(zminfo);
	if(!genesis_data)
		return NULL;
	return genesis_data->map_desc;
}

bool zmapInfoBackupMapDesc(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);

	if (zminfo->genesis_data) {
		StructDestroySafe( parse_GenesisZoneMapInfo, &zminfo->genesis_info );
		StructDestroySafe( parse_GenesisZoneMapData, &zminfo->backup_genesis_data );
		zminfo->backup_genesis_data = StructClone( parse_GenesisZoneMapData, zminfo->genesis_data );
		StructDestroySafe( parse_GenesisZoneMapData, &zminfo->genesis_data );
		zminfo->from_ugc_file = NULL;
		
		zminfo->mod_time++;
		return true;
	} else {
		return false;
	}
}

bool zmapInfoRemoveMapDesc(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);

	if (zminfo->genesis_data) {
		StructDestroySafe( parse_GenesisZoneMapInfo, &zminfo->genesis_info );
		StructDestroySafe( parse_GenesisZoneMapData, &zminfo->backup_genesis_data );
		StructDestroySafe( parse_GenesisZoneMapData, &zminfo->genesis_data );
		
		zminfo->mod_time++;
		return true;
	} else {
		return false;
	}
}

void zmapInfoClearUGCFile(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO(zminfo);

	zminfo->from_ugc_file = NULL;
	zminfo->mod_time++;
}

bool zmapInfoRestoreMapDescFromBackup(SA_PARAM_OP_VALID ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);

	if( zminfo->backup_genesis_data ) {
		StructDestroySafe( parse_GenesisZoneMapData, &zminfo->genesis_data );
		zminfo->genesis_data = StructClone( parse_GenesisZoneMapData, zminfo->backup_genesis_data );
		StructDestroySafe( parse_GenesisZoneMapData, &zminfo->backup_genesis_data );

		// make sure there are no layers or regions still on the zonemap
		eaDestroyStruct( &zminfo->layers, parse_ZoneMapLayerInfo );
		eaDestroyStruct( &zminfo->regions, parse_WorldRegion );
		
		zminfo->mod_time++;
		return true;
	} else {
		return false;
	}
}

//////////////////////////////////////////////////////////////////
// ZoneMapInfo variables
//////////////////////////////////////////////////////////////////
WorldVariableDef ***zmapInfoGetVariableDefs(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	return &zminfo->variable_defs;
}

int zmapInfoGetVariableCount(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return eaSize(&zminfo->variable_defs);
}

SA_RET_OP_VALID WorldVariableDef *zmapInfoGetVariableDef(ZoneMapInfo *zminfo, int var_idx)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	if (!zminfo->variable_defs || var_idx < 0 || var_idx >= eaSize(&zminfo->variable_defs))
		return NULL;
	return zminfo->variable_defs[var_idx];
}

SA_RET_OP_VALID WorldVariableDef *zmapInfoGetVariableDefByName(ZoneMapInfo *zminfo, const char *name)
{
	int i;
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	for(i=eaSize(&zminfo->variable_defs)-1; i>=0; --i)
	{
		if (zminfo->variable_defs[i] && name && (stricmp(zminfo->variable_defs[i]->pcName, name) == 0))
			return zminfo->variable_defs[i];
	}

	return NULL;
}

void zmapInfoAddVariableDef(ZoneMapInfo *zminfo, WorldVariableDef *var)
{
	WorldVariableDef *def;
	VALIDATE_ZMINFO(zminfo);
	def = StructClone(parse_WorldVariableDef, var);
	eaPush(&zminfo->variable_defs, def);
	zminfo->mod_time++;
}

void zmapInfoRemoveVariableDef(ZoneMapInfo *zminfo, int var_idx)
{
	VALIDATE_ZMINFO(zminfo);
	if (var_idx < eaSize(&zminfo->variable_defs))
	{
		assert(zminfo->variable_defs);
		StructDestroy(parse_WorldVariableDef, zminfo->variable_defs[var_idx]);
		eaRemove(&zminfo->variable_defs, var_idx);
		zminfo->mod_time++;
	}
}

void zmapInfoModifyVariableDef(ZoneMapInfo *zminfo, int var_idx, WorldVariableDef *def)
{
	WorldVariableDef *var;
	VALIDATE_ZMINFO(zminfo);
	var = zmapInfoGetVariableDef(zminfo, var_idx);
	if (var)
	{
		StructCopyAll(parse_WorldVariableDef, def, var);
		zminfo->mod_time++;
	}
}

bool zmapInfoValidateVariableDefs(ZoneMapInfo *zminfo, WorldVariableDef **varDefs, const char *reason, const char *filename)
{
	bool result = true;
	int i;
	VALIDATE_ZMINFO_RET(zminfo, true);

	for(i=eaSize(&varDefs)-1; i>= 0; --i) {
		WorldVariableDef *def = zmapInfoGetVariableDefByName(zminfo, varDefs[i]->pcName);
		result &= worldVariableValidateDef(def, varDefs[i], reason, filename);
	}
	return result;
}

// Gets all the names of active map variables
char** zmapInfoGetVariableNames(ZoneMapInfo *zminfo)
{
	char** accum = NULL;
	int it;
	
	VALIDATE_ZMINFO_RET(zminfo, NULL);

	for (it = 0; it != zmapInfoGetVariableCount(zminfo); ++it) {
		WorldVariableDef* def = zmapInfoGetVariableDef(zminfo, it);
		assert(def);
		
		eaPush(&accum, strdup(def->pcName));
	}

	return accum;
}

//////////////////////////////////////////////////////////////////
// ZoneMapInfo GAE layers
//////////////////////////////////////////////////////////////////

int zmapInfoGetGAELayersCount(const ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return eaSize(&zminfo->global_gaelayer_defs);
}

SA_RET_OP_VALID GlobalGAELayerDef *zmapInfoGetGAELayerDef(ZoneMapInfo *zminfo, int var_idx)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	if (!zminfo->global_gaelayer_defs || var_idx < 0 || var_idx >= eaSize(&zminfo->global_gaelayer_defs))
		return NULL;
	return zminfo->global_gaelayer_defs[var_idx];
}

void zmapInfoAddGAELayerDef(ZoneMapInfo *zminfo, GlobalGAELayerDef *var)
{
	GlobalGAELayerDef *def;
	VALIDATE_ZMINFO(zminfo);

	def = StructClone(parse_GlobalGAELayerDef, var);
	eaPush(&zminfo->global_gaelayer_defs, def);

	zminfo->mod_time++;
}

void zmapInfoRemoveGAELayerDef(ZoneMapInfo *zminfo, int var_idx)
{
	VALIDATE_ZMINFO(zminfo);
	if (var_idx < eaSize(&zminfo->global_gaelayer_defs))
	{
		assert(zminfo->global_gaelayer_defs);
		StructDestroy(parse_GlobalGAELayerDef, zminfo->global_gaelayer_defs[var_idx]);
		eaRemove(&zminfo->global_gaelayer_defs, var_idx);

		zminfo->mod_time++;
	}
}

void zmapInfoModifyGAELayerDef(ZoneMapInfo *zminfo, int var_idx, GlobalGAELayerDef *def)
{
	GlobalGAELayerDef *var;
	VALIDATE_ZMINFO(zminfo);
	var = zmapInfoGetGAELayerDef(zminfo, var_idx);
	if (var)
	{
		StructCopyAll(parse_GlobalGAELayerDef, def, var);
		zminfo->mod_time++;
	}
}

//////////////////////////////////////////////////////////////////
// ZoneMapInfo Regions
//////////////////////////////////////////////////////////////////

WorldRegion **zmapInfoGetWorldRegions(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);

	if (zminfo->genesis_data && eaSize(&zminfo->regions) == 0)
		Errorf("Asking for world regions of a non-loaded Genesis zonemap, '%s'.", zminfo->map_name);
	return zminfo->regions;
}

WorldRegion **zmapInfoGetAllWorldRegions(ZoneMapInfo *zminfo)
{
	static WorldRegion **regions = NULL;
	VALIDATE_ZMINFO_RET(zminfo, NULL);

	if (zminfo->genesis_data && eaSize(&zminfo->regions) == 0)
		Errorf("Asking for world regions of a non-loaded Genesis zonemap, '%s'.", zminfo->map_name);

	eaClearFast(&regions);
	eaPushEArray(&regions, &zminfo->regions);

	FOR_EACH_IN_EARRAY_FORWARDS(zminfo->secondary_maps, SecondaryZoneMap, secondary)
	{
		ZoneMapInfo *secondZMInfo = RefSystem_ReferentFromString(g_ZoneMapDictionary, secondary->map_name);

		if(secondZMInfo)
			eaPushEArray(&regions, &secondZMInfo->regions);
	}
	FOR_EACH_END;

	if(zminfo->genesis_info && zminfo->genesis_info->external_map)
	{
		ZoneMapInfo *externalMap = RefSystem_ReferentFromString(g_ZoneMapDictionary, zminfo->genesis_info->external_map);

		if(externalMap)
			eaPushEArray(&regions, &externalMap->regions);
	}

	return regions;
}

U32 zmapInfoHasSpaceRegion(ZoneMapInfo *zminfo)
{
	int i;
	WorldRegion** regions = NULL;
	
	VALIDATE_ZMINFO_RET(zminfo, 0);

	regions = zmapInfoGetAllWorldRegions(zminfo);

	for(i=0; i<eaSize(&regions); i++)
	{
		WorldRegionType wrt = worldRegionGetType(regions[i]);
		if(wrt == WRT_Space || wrt == WRT_SectorSpace)
			return true;
	}

	return false;
}

//adds a default region to the zmap.  Called when the zmap's region list is found empty.
static void zmapInfoAddDefaultRegion(ZoneMapInfo *zminfo)
{
	WorldRegion *default_region = StructCreate(parse_WorldRegion);
	eaPush(&zminfo->regions, default_region);
}

void zmapInfoSetRegionOverrideCubemap(ZoneMapInfo *zminfo, int idx, const char *override_cubemap)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->regions))
		return;

	if (idx == 0 && !zminfo->regions && override_cubemap)
	{
		zmapInfoAddDefaultRegion(zminfo);
	}

	assert(zminfo->regions);
	zminfo->regions[idx]->override_cubemap = allocAddString(override_cubemap);
	zminfo->mod_time++;
}

void zmapInfoSetRegionAllowedPetsPerPlayer(ZoneMapInfo *zminfo, int idx, S32 iAllowedPetsPerPlayer)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->regions))
		return;

	if (idx == 0 && !zminfo->regions && iAllowedPetsPerPlayer==0)
	{
		zmapInfoAddDefaultRegion(zminfo);
	}

	assert(zminfo->regions);
	if ( iAllowedPetsPerPlayer == 0 )
	{
		zminfo->regions[idx]->pRegionRulesOverride.iAllowedPetsPerPlayer = 0;
	}
	else
	{
		zminfo->regions[idx]->pRegionRulesOverride.iAllowedPetsPerPlayer = iAllowedPetsPerPlayer;
	}
	zminfo->mod_time++;
}

void zmapInfoSetRegionUnteamedPetsPerPlayer(ZoneMapInfo *zminfo, int idx, S32 iUnteamedPetsPerPlayer)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->regions))
		return;

	if (idx == 0 && !zminfo->regions && iUnteamedPetsPerPlayer==0)
	{
		zmapInfoAddDefaultRegion(zminfo);
	}

	assert(zminfo->regions);
	if ( iUnteamedPetsPerPlayer == 0 )
	{
		zminfo->regions[idx]->pRegionRulesOverride.iUnteamedPetsPerPlayer = 0;
	}
	else
	{
		zminfo->regions[idx]->pRegionRulesOverride.iUnteamedPetsPerPlayer = iUnteamedPetsPerPlayer;
	}
	zminfo->mod_time++;
}

void zmapInfoSetRegionVechicleRules(ZoneMapInfo *zminfo, int idx, S32 eVehicleRules)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->regions))
		return;

	if (idx == 0 && !zminfo->regions && eVehicleRules==0)
	{
		zmapInfoAddDefaultRegion(zminfo);
	}

	assert(zminfo->regions);
	
	zminfo->regions[idx]->pRegionRulesOverride.eVehicleRules = eVehicleRules;
	zminfo->mod_time++;
}

void zmapInfoSetRegionSkyGroup(ZoneMapInfo *zminfo, int idx, SkyInfoGroup *sky_group)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->regions))
		return;

	if (idx == 0 && !zminfo->regions && sky_group)
	{
		zmapInfoAddDefaultRegion(zminfo);
	}

	assert(zminfo->regions);
	zminfo->regions[idx]->sky_group = sky_group;
	zminfo->mod_time++;
}

void zmapInfoSetRegionWorldGeoClustering(ZoneMapInfo *zminfo, int idx, bool bWorldGeoClustering)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->regions))
		return;

	if (idx == 0 && !zminfo->regions)
	{
		zmapInfoAddDefaultRegion(zminfo);
	}

	assert(zminfo->regions);
	worldRegionSetWorldGeoClustering(zminfo->regions[idx], bWorldGeoClustering);
	zminfo->mod_time++;
}

void zmapInfoSetRegionIndoorLighting(ZoneMapInfo *zminfo, int idx, bool bIndoorLighting)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->regions))
		return;

	if (idx == 0 && !zminfo->regions)
	{
		zmapInfoAddDefaultRegion(zminfo);
	}

	assert(zminfo->regions);
	worldRegionSetIndoorLighting(zminfo->regions[idx], bIndoorLighting);
	zminfo->mod_time++;
}

void zmapInfoSetRegionType(ZoneMapInfo *zminfo, int idx, WorldRegionType type)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->regions))
		return;

	if (idx == 0 && !zminfo->regions)
	{
		zmapInfoAddDefaultRegion(zminfo);
	}

	assert(zminfo->regions);
	worldRegionSetType(zminfo->regions[idx], type);
	zminfo->mod_time++;
}

//////////////////////////////////////////////////////////////////
// ZoneMapInfo Basic Editing Functions
//////////////////////////////////////////////////////////////////

ZoneMapInfo *zmapInfoNew(const char *filename, const char *map_name)
{
	ZoneMapInfo *zminfo = StructCreate(parse_ZoneMapInfo);

	zminfo->filename = allocAddFilename(filename);
	zminfo->map_name = allocAddString(map_name);
	zminfo->mod_time++;
	zminfo->is_new = true;

	return zminfo;
}

ZoneMapInfo *zmapInfoCopy(ZoneMapInfo *zminfo_src, const char *filename, const char *map_name)
{
	ZoneMapInfo *zminfo;
	VALIDATE_ZMINFO_RET(zminfo_src, NULL);

	zminfo = StructClone(parse_ZoneMapInfo, zminfo_src);
	if (!zminfo)
		zminfo = StructCreate(parse_ZoneMapInfo);
	ANALYSIS_ASSUME(zminfo != NULL);

	StructDeInit(parse_DisplayMessage, &zminfo->display_name);
	zminfo->filename = allocAddFilename(filename);
	zminfo->map_name = allocAddString(map_name);
	zminfo->mod_time++;
	zminfo->is_new = true;

	return zminfo;
}

ZoneMapInfo *zmapInfoCreateUGCDummy(const char *map_name, const char *project_prefix, const char *map_filename, const char *display_name,
									ContainerID ugcProjectID, const char *spawn_name, ZoneMapLightOverrideType light_type)
{
	char buf[MAX_PATH];
	char ns[RESOURCE_NAME_MAX_SIZE], base[RESOURCE_NAME_MAX_SIZE];
	Message *new_message;
	// Initialize ZoneMapInfo
	ZoneMapInfo *zminfo = StructCreate(parse_ZoneMapInfo);
	zminfo->map_name = map_name;
	zminfo->light_override = light_type;

	sprintf(buf, "UGC.ZoneMap.%s.DisplayName", map_name);
	new_message = langCreateMessage(buf, NULL, NULL, display_name);
	zmapInfoSetDisplayNameMessage(zminfo, new_message);

	if (resExtractNameSpace(map_name, ns, base))
		sprintf(buf, "ns/%s/Maps/%s/%s/%s.zone", ns, project_prefix, base, base);
	else
		sprintf(buf, "Maps/%s/%s/%s.zone", project_prefix, map_name, map_name);
	zminfo->filename = allocAddFilename(buf);

	zminfo->ugcProjectID = ugcProjectID;
	zminfo->from_ugc_file = map_filename;
	zminfo->map_type = ZMTYPE_MISSION; // Mission map type is the default
	zminfo->start_spawn_name = spawn_name ? StructAllocString(spawn_name) : NULL;
	zminfo->disable_visited_tracking = true;

	// Make this saveable
	zminfo->mod_time = 1;
	zminfo->is_new = 1;
	return zminfo;
}

void zmapInfoDestroyUGCDummy(ZoneMapInfo *zminfo)
{
	StructDestroy(parse_ZoneMapInfo, zminfo);
}

void zmapInfoSetModified(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO(zminfo);
	if (!zmapInfoLocked(zminfo))
	{
		Alertf("ZoneMap: %s -- Attempting to update a non-locked zonemap.",
			   zminfo->map_name);
		return;
	}
	zminfo->mod_time++;
}

void zmapInfoSetName(ZoneMapInfo *zminfo, const char *map_name)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->new_map_name = allocAddString(map_name);
	zminfo->mod_time++;
}

void zmapInfoSetMapType(ZoneMapInfo *zminfo, ZoneMapType map_type)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->map_type = map_type;
	if (zminfo->map_type != ZMTYPE_OWNED)
	{
		zminfo->guild_owned = false;
		zminfo->guild_not_required = false;
	}
	zminfo->mod_time++;
}

void zmapInfoSetRespawnType(ZoneMapInfo *zminfo, ZoneRespawnType respawn_type)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->eRespawnType = respawn_type;
	zminfo->mod_time++;
}

void zmapInfoSetRespawnWaveTime(ZoneMapInfo *zminfo, U32 respawn_time)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->RespawnWaveTime = respawn_time;
	zminfo->mod_time++;
}

void zmapInfoSetMapLevel(ZoneMapInfo *zminfo, U32 level)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->level = level;
	zminfo->mod_time++;
}

void zmapInfoSetMapDifficulty(ZoneMapInfo *zminfo, S32 eDifficulty)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->difficulty = eDifficulty;
	zminfo->mod_time++;
}

void zmapInfoSetMapForceTeamSize(ZoneMapInfo *zminfo, U32 force_team_size)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->force_team_size = force_team_size;
	zminfo->mod_time++;
}

void zmapInfoSetMapIgnoreTeamSizeBonusXP(ZoneMapInfo *zminfo, bool bIgnoreTeamSizeBonusXP)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->bIgnoreTeamSizeBonusXP = bIgnoreTeamSizeBonusXP;
	zminfo->mod_time++;
}

void zmapInfoSetMapUsedInUGC(ZoneMapInfo *zminfo, ZoneMapUGCUsage eUsedInUGC)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->eUsedInUGC = eUsedInUGC;
	zminfo->mod_time++;
}

void zmapInfoSetDefaultQueueDef(ZoneMapInfo *zminfo, const char *pchQueueDef)
{
	VALIDATE_ZMINFO(zminfo);
	if(zminfo->default_queue)
		StructFreeString(zminfo->default_queue);
	if(pchQueueDef)
		zminfo->default_queue = StructAllocString(pchQueueDef);

	zminfo->mod_time++;
}

void zmapInfoSetDefaultPVPGameType(ZoneMapInfo *zminfo, const char *pchGameType)
{
	VALIDATE_ZMINFO(zminfo);
	if(zminfo->default_gametype)
		StructFreeString(zminfo->default_gametype);
	if(pchGameType)
		zminfo->default_gametype = StructAllocString(pchGameType);

	zminfo->mod_time++;
}

void zmapInfoSetDisplayNameMessage(ZoneMapInfo *zminfo, const Message *message)
{
	DisplayMessage *zmapMessage;
	VALIDATE_ZMINFO(zminfo);

	zmapMessage = &zminfo->display_name;
	if (message)
	{
		// create an editor copy
		if (!zmapMessage->pEditorCopy)
			langMakeEditorCopy(parse_DisplayMessage, zmapMessage, true);

		StructCopyAll(parse_Message, message, zmapMessage->pEditorCopy);
	}
	else
	{
		StructDeInit(parse_DisplayMessage, zmapMessage);
	}
	zminfo->mod_time++;
}

void zmapInfoClearPrivacy(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO(zminfo);
	eaDestroyEx(&zminfo->private_to, StructFreeString);
	zminfo->mod_time++;
}

void zmapInfoAddPrivacy(ZoneMapInfo *zminfo, const char *privacy)
{
	char formatted_privacy[128];
	VALIDATE_ZMINFO(zminfo);

	if (!privacy)
		privacy = gimmeDLLQueryUserName();
	sprintf(formatted_privacy, "%s", privacy);

	// remove leading and trailing spaces
	removeLeadingAndFollowingSpaces(formatted_privacy);

	if (!formatted_privacy[0])
		return;
	if (eaFindString(&zminfo->private_to, formatted_privacy) < 0)
	{
		eaPush(&zminfo->private_to, StructAllocString(formatted_privacy));
	}
	zminfo->mod_time++;
}

PhotoOptions* zmapInfoGetPhotoOptions(ZoneMapInfo *zminfo, bool make)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	if(make && !zminfo->photo_options && zmapInfoLocked(zminfo))
	{
		zminfo->photo_options = StructCreate(parse_PhotoOptions);
		zminfo->mod_time++;
	}
	return zminfo->photo_options;
}

void zmapInfoSetWindLargeObjectRadiusThreshold(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, F32 radius)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->wind_large_object_radius_threshold = radius;
	zminfo->mod_time++;
}

void zmapInfoSetCollectDoorDestStatus(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool state)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->collect_door_dest_status = state;
	zminfo->mod_time++;
}

void zmapInfoSetDisableDuels(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool disable)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->disable_duels = disable;
	zminfo->mod_time++;
}

void zmapInfoSetPowersRequireValidTarget(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->powers_require_valid_target = enable;
	zminfo->mod_time++;
}

void zmapInfoSetEnableShardVariables(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->enable_shard_variables = enable;
	zminfo->mod_time++;
}

void zmapInfoSetDisableInstanceChanging(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool disable)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->disable_instance_change = disable;
	zminfo->mod_time++;
}

void zmapInfoSetTeamNotRequired(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->team_not_required = enable;
	zminfo->mod_time++;
}

void zmapInfoSetGuildNotRequired(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable)
{
	VALIDATE_ZMINFO(zminfo);
	if (zminfo->guild_owned)
		zminfo->guild_not_required = enable;
	else
		zminfo->guild_not_required = false;
	zminfo->mod_time++;
}

void zmapInfoSetGuildOwned(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable)
{
	VALIDATE_ZMINFO(zminfo);
	if (zminfo->map_type == ZMTYPE_OWNED)
		zminfo->guild_owned = enable;
	else
		zminfo->guild_owned = false;
	zminfo->mod_time++;
}

void zmapInfoSetStartSpawnName(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, const char *spawn_name)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->start_spawn_name = StructAllocString(spawn_name);
	zminfo->mod_time++;
}

void zmapInfoSetTerrainStaticLighting(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool enable)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->terrain_static_lighting = enable;
	zminfo->mod_time++;
}

void zmapInfoSetDisableVisitedTracking(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool val)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->disable_visited_tracking = val;
	zminfo->mod_time++;
}

void zmapInfoSetRecordPlayerMatchStats(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool val)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->record_player_match_stats = val;
	zminfo->mod_time++;
}

void zmapInfoSetEnableUpsellFeatures(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, bool val)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->enable_upsell_features = val;
	zminfo->mod_time++;
}

static void zmapInfoRespawnTimeCheck(SA_PARAM_NN_VALID ZoneMapInfo *zminfo)
{
	if (!zminfo->respawn_data->min_time &&
		!zminfo->respawn_data->increment)
	{
		StructDestroySafe(parse_WorldRespawnData, &zminfo->respawn_data);
	}
}

void zmapInfoSetRespawnTimes(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 min, U32 max, U32 increment)
{
	VALIDATE_ZMINFO(zminfo);
	if (!zminfo->respawn_data)
		zminfo->respawn_data = StructCreate(parse_WorldRespawnData);

	zminfo->respawn_data->min_time = min;
	zminfo->respawn_data->max_time = max;
	zminfo->respawn_data->increment = increment;
	zmapInfoRespawnTimeCheck(zminfo);
	zminfo->mod_time++;
}

void zmapInfoSetRespawnMinTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 min)
{
	VALIDATE_ZMINFO(zminfo);
	if (!zminfo->respawn_data)
		zminfo->respawn_data = StructCreate(parse_WorldRespawnData);

	zminfo->respawn_data->min_time = min;
	zmapInfoRespawnTimeCheck(zminfo);
	zminfo->mod_time++;
}

void zmapInfoSetRespawnMaxTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 max)
{
	VALIDATE_ZMINFO(zminfo);
	if (!zminfo->respawn_data)
		zminfo->respawn_data = StructCreate(parse_WorldRespawnData);

	zminfo->respawn_data->max_time = max;
	zmapInfoRespawnTimeCheck(zminfo);
	zminfo->mod_time++;
}

void zmapInfoSetRespawnIncrementTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 increment)
{
	VALIDATE_ZMINFO(zminfo);
	if (!zminfo->respawn_data)
		zminfo->respawn_data = StructCreate(parse_WorldRespawnData);

	zminfo->respawn_data->increment = increment;
	zmapInfoRespawnTimeCheck(zminfo);
	zminfo->mod_time++;
}

void zmapInfoSetRespawnAttritionTime(SA_PARAM_OP_VALID ZoneMapInfo *zminfo, U32 attrition)
{
	VALIDATE_ZMINFO(zminfo);
	if (!zminfo->respawn_data)
		zminfo->respawn_data = StructCreate(parse_WorldRespawnData);
	if (zminfo->respawn_data->increment)
		zminfo->respawn_data->attrition_time = attrition;
	else
		zminfo->respawn_data->attrition_time = 0;
	zmapInfoRespawnTimeCheck(zminfo);
	zminfo->mod_time++;
}

#ifndef NO_EDITORS

//////////////////////////////////////////////////////////////////
// ZoneMapInfo Genesis Data Editing
//////////////////////////////////////////////////////////////////

GenesisZoneMapData *zmapInfoAddGenesisData(ZoneMapInfo *zminfo, UGCMapType ugc_type)
{
	VALIDATE_ZMINFO_RET(zminfo, NULL);
	if (zminfo->genesis_data)
		return zminfo->genesis_data;

	zminfo->genesis_data = StructCreate(parse_GenesisZoneMapData);
	zminfo->genesis_data->map_desc = StructCreate(parse_GenesisMapDescription);
	zminfo->genesis_data->map_desc->version = GENESIS_MAP_DESC_VERSION;
	zminfo->mod_time++;
	return zminfo->genesis_data;
}

void zmapInfoSetMapDesc(ZoneMapInfo *zminfo, GenesisMapDescription *map_desc)
{
	U32 old_seed=0, old_detail_seed=0;
	bool skip_terrain_update = false;
	VALIDATE_ZMINFO(zminfo);
	if (zminfo->genesis_data)
	{
		skip_terrain_update = genesisTerrainCanSkipUpdate(zminfo->genesis_data->map_desc, map_desc);
		old_seed = zminfo->genesis_data->seed;
		old_detail_seed = zminfo->genesis_data->detail_seed;
		StructDestroy(parse_GenesisZoneMapData, zminfo->genesis_data);
	}
	zminfo->genesis_data = StructCreate(parse_GenesisZoneMapData);
	zminfo->genesis_data->seed = old_seed;
	zminfo->genesis_data->detail_seed = old_detail_seed;
	zminfo->genesis_data->map_desc = map_desc;
	zminfo->genesis_data->skip_terrain_update = skip_terrain_update;
	zminfo->mod_time++;
}

void zmapInfoSetGenesisZoneMissions(ZoneMapInfo *zminfo, GenesisZoneMission **data)
{
	VALIDATE_ZMINFO(zminfo);
	if (!zminfo->genesis_data)
		zminfo->genesis_data = StructCreate(parse_GenesisZoneMapData);
	zminfo->genesis_data->genesis_mission = data;
	zminfo->mod_time++;
}

void zmapInfoSetSharedGenesisZoneChallenges(ZoneMapInfo *zminfo, GenesisMissionZoneChallenge **challenges)
{
	VALIDATE_ZMINFO(zminfo);
	if (!zminfo->genesis_data)
		zminfo->genesis_data = StructCreate(parse_GenesisZoneMapData);
	zminfo->genesis_data->genesis_shared_challenges = challenges;
	zminfo->mod_time++;
}

// TomY ENCOUNTER_HACK
void zmapInfoSetEncounterOverrides(ZoneMapInfo *zminfo, GenesisProceduralEncounterProperties **properties)
{
	VALIDATE_ZMINFO(zminfo);
	if (!zminfo->genesis_data)
		zminfo->genesis_data = StructCreate(parse_GenesisZoneMapData);
	eaDestroyStruct(&zminfo->genesis_data->encounter_overrides, parse_GenesisProceduralEncounterProperties);		
	zminfo->genesis_data->encounter_overrides = properties;
	zminfo->mod_time++;
}

#endif

//////////////////////////////////////////////////////////////////
// ZoneMapInfo Layer Editing
//////////////////////////////////////////////////////////////////

void zmapInfoAddLayer(ZoneMapInfo *zminfo, const char *filename, const char *region_name)
{
	ZoneMapLayerInfo *new_layer;
	VALIDATE_ZMINFO(zminfo);
	new_layer = StructCreate(parse_ZoneMapLayerInfo);
	new_layer->filename = allocAddFilename(filename);
	new_layer->region_name = allocAddString(region_name);
	eaPush(&zminfo->layers, new_layer);
	zminfo->mod_time++;
}

void zmapInfoSetLayerRegion(ZoneMapInfo *zminfo, int idx, const char *region_name)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->layers))
		return;

	assert(zminfo->layers);

	zminfo->layers[idx]->region_name = allocAddString(region_name);
	zminfo->mod_time++;
}

void zmapInfoRemoveLayer(ZoneMapInfo *zminfo, int idx)
{
	VALIDATE_ZMINFO(zminfo);
	if (idx < 0 || idx >= eaSize(&zminfo->layers))
		return;

	StructDestroy(parse_ZoneMapLayerInfo, zminfo->layers[idx]);
	eaRemove(&zminfo->layers, idx);
	zminfo->mod_time++;
}

//////////////////////////////////////////////////////////////////
// ZoneMapInfo Locking
//////////////////////////////////////////////////////////////////


static FileScanAction serverCheckoutDirScan( char* dir, struct _finddata32_t* data, char*** accum )
{
	char fullPath[ MAX_PATH ];
	sprintf( fullPath, "%s/%s", dir, data->name );
	eaPush( accum, strdup( fullPath ));

	return FSA_EXPLORE_DIRECTORY;
}

static bool serverCheckoutDir( const char* dirname )
{
	int ret;
	char** accum = NULL;

	fileScanAllDataDirs( dirname, serverCheckoutDirScan, &accum );

	{
		int it;

		for( it = 0; it != eaSize( &accum ); ++it ) {
			if( !gimmeDLLQueryIsFileLatest( accum[it] )) {
				Alertf( "Error: file (%s) unable to be checked out, someone else has "
					"changed it since you last got latest.",
					accum[it] );
				return false;
			}
		}
	}

	ret = gimmeDLLDoOperations( accum, GIMME_CHECKOUT, 0 );
	eaDestroyEx( &accum, NULL );

	if( ret != GIMME_NO_ERROR && ret != GIMME_ERROR_NOT_IN_DB && ret != GIMME_ERROR_NO_DLL ) {
		Alertf( "Error: unable to checkout dir (%s).", dirname );
		return false;
	}
	return true;
}

void zmapInfoLockQueueActions(ResourceActionList* actions, ZoneMapInfo *zminfo, bool forceGenesisDataLock)
{
	if (!zminfo)
		return;

	if (forceGenesisDataLock || zminfo->genesis_data)
	{
		bool succeeded = true;
		char zmapDir[ MAX_PATH ];
		char path[ MAX_PATH ];

		strcpy( zmapDir, zminfo->filename );

		getDirectoryName( zmapDir );

		sprintf( path, "%s/contacts/", zmapDir );
		succeeded &= serverCheckoutDir(path);

		sprintf( path, "%s/messages/", zmapDir );
		succeeded &= serverCheckoutDir(path);

		sprintf( path, "%s/missions/", zmapDir );
		succeeded &= serverCheckoutDir(path);

		if( !succeeded ) {
			Alertf( "Checkout of Genesis missions, contacts, and messages FAILED." );
			return;
		}
	}

	resAddRequestLockResource( actions, g_ZoneMapDictionary, zminfo->map_name, zminfo);
}

void zmapInfoLockEx(ZoneMapInfo *zminfo, bool forceGenesisDataLock)
{
	VALIDATE_ZMINFO(zminfo);
	if (forceGenesisDataLock || !zmapInfoLocked(zminfo))
	{
		ResourceActionList actions = { 0 };

		resSetDictionaryEditMode(g_ZoneMapDictionary, true);
		resSetDictionaryEditMode( gMessageDict, true );
		zmapInfoLockQueueActions( &actions, zminfo, forceGenesisDataLock );
		resRequestResourceActions( &actions );
		StructDeInit( parse_ResourceActionList, &actions );
	}
}

bool zmapInfoLocked(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return zminfo->is_new || resIsWritable(g_ZoneMapDictionary, zminfo->map_name);
}

U32 zmapInfoGetLockOwner(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return resGetLockOwner(g_ZoneMapDictionary, zminfo->map_name);
}

bool zmapInfoGetLockOwnerIsZero(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return resGetLockOwnerIsZero(g_ZoneMapDictionary, zminfo->map_name);
}

//////////////////////////////////////////////////////////////////
// ZoneMapInfo Saving
//////////////////////////////////////////////////////////////////

bool zmapInfoSave(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	if (zminfo->mod_time > 0 && zmapInfoLocked(zminfo))
	{
		ResourceActionList actions = { 0 };
		WorldRegion **regions = zminfo->regions;

		if (zminfo->genesis_data)
		{
			zminfo->regions = NULL;
		}

		// fixup display name message
		if (!zminfo->display_name.pEditorCopy)
			langMakeEditorCopy(parse_DisplayMessage, &zminfo->display_name, true);
		if (zminfo->display_name.pEditorCopy)
		{
			char ms_filename[MAX_PATH];

			zminfo->display_name.pEditorCopy->pcMessageKey = allocAddString(zmapInfoGetDefaultDisplayNameMsgKey(zminfo));
			msgSetScope(zminfo->display_name.pEditorCopy, zmapInfoGetDefaultMessageScope(zminfo));

			sprintf(ms_filename, "%s.ms", zminfo->filename);
			msgSetFilename(zminfo->display_name.pEditorCopy, ms_filename);
		}

		//if( wlIsClient() ) {
			resSetDictionaryEditMode(g_ZoneMapDictionary, true);
			resSetDictionaryEditMode( gMessageDict, true );
		//}
		zminfo->saving = true;
		if (zminfo->new_map_name)
		{
			const char *tempname = zminfo->map_name;
			// Delete old resource
			resAddRequestLockResource( &actions, g_ZoneMapDictionary, zminfo->map_name, zminfo);
			resAddRequestSaveResource( &actions, g_ZoneMapDictionary, zminfo->map_name, NULL);
			zminfo->map_name = zminfo->new_map_name;
			// Create new resource
			resAddRequestLockResource( &actions, g_ZoneMapDictionary, zminfo->new_map_name, zminfo);
			resAddRequestSaveResource( &actions, g_ZoneMapDictionary, zminfo->new_map_name, zminfo);
			// Set the name back temporarily until the whole thing succeeds
			zminfo->map_name = tempname;
		}
		else
		{
			resAddRequestLockResource( &actions, g_ZoneMapDictionary, zminfo->map_name, zminfo);
			resAddRequestSaveResource( &actions, g_ZoneMapDictionary, zminfo->map_name, zminfo);
		}
		resRequestResourceActions( &actions );

		StructDeInit( parse_ResourceActionList, &actions );

		zminfo->regions = regions;
		return true;
	}
	return false;
}

bool zmapInfoGetUnsaved(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, false);
	return (zminfo->mod_time > 0);
}

void zmapInfoSetSaved(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO(zminfo);
	zminfo->mod_time = 0;
	zminfo->is_new = false;
	if (zminfo->new_map_name)
	{
		zminfo->map_name = zminfo->new_map_name;
		zminfo->new_map_name = NULL;
	}
}

int zmapInfoGetModTime(ZoneMapInfo *zminfo)
{
	VALIDATE_ZMINFO_RET(zminfo, 0);
	return zminfo->mod_time;
}

const char *zmapInfoGetDefaultMessageScope(ZoneMapInfo *zminfo)
{
	const char *filename;
	VALIDATE_ZMINFO_RET(zminfo, NULL);

	filename = zmapInfoGetFilename(zminfo);
	if (filename)
	{
		char scope[MAX_PATH];

		changeFileExt(filename, "", scope);
		strchrReplace(scope, '.', '_');
		strchrReplace(scope, '/', '.');
		strchrReplace(scope, '\\', '.');
		return allocAddString(scope);
	}

	return NULL;
}


//////////////////////////////////////////////////////////////////
// ZoneMap functions
//////////////////////////////////////////////////////////////////

#define VALIDATE_ZMAP(zmap) if (!(zmap)) (zmap) = world_grid.active_map; if (!(zmap)) return;
#define VALIDATE_ZMAP_RET(zmap, ret) if (!(zmap)) (zmap) = world_grid.active_map; if (!(zmap)) return (ret);

ZoneMapInfo *zmapGetInfo(ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, NULL);
	return &zmap->map_info;
}

static const char *getMapDefaultName(const ZoneMap *zmap)
{
	static char path[MAX_PATH];
	const char *filename = zmapGetFilename(zmap);
	getFileNameNoExtNoDirs(path, filename?filename:"");
	return path;
}

void zmapFixupLayerFilenames(ZoneMap *zmap)
{
	char zmap_path[MAX_PATH], *s;
	int i;

	VALIDATE_ZMAP(zmap);

	strcpy(zmap_path, zmap->map_info.filename);
	s = strrchr(zmap_path, '/');
	if (s)
		*(s+1) = 0;

	for (i = 0; i < eaSize(&zmap->layers); ++i)
	{
		ZoneMapLayer *layer = zmap->layers[i];
		char ns[MAX_PATH], base_name[MAX_PATH], layer_filename[MAX_PATH];

		layer->zmap_parent = zmap;

		// make relative layer paths absolute
		if (layer->filename && strchr(layer->filename, '/') == NULL)
		{
			sprintf(layer_filename, "%s%s", zmap_path, layer->filename);
		}
		else
		{
			strcpy(layer_filename, layer->filename);
		}

		if (resExtractNameSpace(layer_filename, ns, base_name))
			sprintf(layer_filename, NAMESPACE_PATH"%s/%s", ns, base_name);

		layer->filename = allocAddFilename(layer_filename);
	}
}

const char* zmapGetFilename(const ZoneMap *zmap)
{
	//const char *no_ns_path;
	VALIDATE_ZMAP_RET(zmap, NULL);
	//if ((no_ns_path = strrchr(zmap->map_info.filename, ':')))
	//	return (no_ns_path+1);
	return zmap->map_info.filename;
}

const char* zmapGetName(const ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, NULL);
	return zmap->map_info.map_name;
}

GenesisZoneMission **zmapGetZoneMissions(ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, NULL);
	return SAFE_MEMBER2(&zmap->map_info, genesis_data, genesis_mission);
}

//////////////////////////////////////////////////////////////////////////

void zmapSetPreviewFlag(SA_PARAM_OP_VALID ZoneMap *zmap)
{
	VALIDATE_ZMAP(zmap);
	zmap->genesis_data_preview = true;
}

//////////////////////////////////////////////////////////////////////////

static void zmapLoadInternal(ZoneMap *zmap, bool load_layers)
{
	int i;

	zmap->world_cell_data.fx_id_counter = 1;
	zmap->world_cell_data.animation_id_counter = 1;

	assert(!zmap->world_cell_data.drawable_pool.total_material_draw_count);
	assert(!zmap->world_cell_data.drawable_pool.total_model_draw_count);
	assert(!zmap->world_cell_data.drawable_pool.total_subobject_count);
	assert(!zmap->world_cell_data.drawable_pool.total_draw_list_count);

	for (i = 0; i < eaSize(&zmap->map_info.regions); ++i)
	{
		initWorldRegion(zmap->map_info.regions[i]);
		zmap->map_info.regions[i]->zmap_parent = zmap;
	}

	if (eaSize(&zmap->map_info.regions) <= 0 || zmap->map_info.regions[0]->name)
		createWorldRegion(zmap, NULL);

	if (load_layers)
	{
		for (i = 0; i < eaSize(&zmap->layers); ++i)
			layerLoadGroupSource(zmap->layers[i], zmap, NULL, false);

		zmapRecalcBounds(zmap, true);
	}
	else
	{
		for (i = 0; i < eaSize(&zmap->layers); ++i)
			zmap->layers[i]->zmap_parent = zmap;
	}

	zmap->zmap_scope = worldZoneMapScopeCreate();

	if (wlIsServer())
	{
		if (zmap->map_info.mastermind_def && wl_state.mastermind_def_updated_callback)
			wl_state.mastermind_def_updated_callback(zmap->map_info.mastermind_def);

		for (i = 0; i < eaSize(&zmap->map_info.variable_defs); i++) {
			worldVariableValidateDef(zmap->map_info.variable_defs[i], zmap->map_info.variable_defs[i], zmap->map_info.map_name, zmap->map_info.filename);
			worldVariableDefGenerateExpressions(zmap->map_info.variable_defs[i], "ZoneMap", zmapGetFilename(zmap));
		}
	}
}

ZoneMap *zmapLoad(ZoneMapInfo *zminfo)
{
	int i, j;
	ZoneMap *zmap;

	PERFINFO_AUTO_START_FUNC();

	zmap = calloc(1, sizeof(ZoneMap));

	// Initialize based on info
	StructCopy(parse_ZoneMapInfo, zminfo, &zmap->map_info, 0, 0, 0);
	if (!isProductionEditMode())
		zmap->last_saved_info = StructClone(parse_ZoneMapInfo, zminfo);

	if (eaSize(&zminfo->layers) == 0 && zminfo->genesis_info)
	{
		zmapLoadInternal(zmap, false);

		// Make Genesis Layers
		genesisMakeLayers(zmap);
		zmapFixupLayerFilenames(zmap);
		zmap->map_info.mod_time = 0;
	}
	else
	{
		for (i = 0; i < eaSize(&zminfo->layers); i++)
		{
			ZoneMapLayer *layer = StructCreate(parse_ZoneMapLayer);
			layer->filename = allocAddFilename(zminfo->layers[i]->filename);
			layer->region_name = allocAddString(zminfo->layers[i]->region_name);
			layer->genesis = zminfo->layers[i]->genesis;
			eaPush(&zmap->layers, layer);
		}
			
		zmapFixupLayerFilenames(zmap);
		zmapLoadInternal(zmap, false);
	}

	zmap->genesis_view_type = GENESIS_VIEW_FULL;

	collCacheInit(zmap->map_info.map_name, zmap->map_info.map_type);
	// error checking
	if (wlIsClient())
	{
		for (i = 0; i < eaSize(&zmap->map_info.regions); ++i)
		{
			WorldRegion *region = zmap->map_info.regions[i];
			if (region->sky_group)
			{
				for (j = 0; j < eaSize(&region->sky_group->override_list); ++j)
				{
					SkyInfoOverride *sky_override = region->sky_group->override_list[j];
					if (!GET_REF(sky_override->sky))
						ErrorFilenamef(zmapGetFilename(zmap), "Region %s references unknown sky file %s.", region->name?region->name:"Default", REF_STRING_FROM_HANDLE(sky_override->sky));
				}
				if (region->override_cubemap && wl_state.tex_find_func && !wl_state.tex_find_func(region->override_cubemap, false, WL_FOR_WORLD))
				{
					ErrorFilenamef(zmapGetFilename(zmap), "Region %s references unknown texture \"%s\".", region->name?region->name:"Default", region->override_cubemap);
				}
			}

			// Only make a stashtable if there are enough swaps to justify it
			if (eaSize(&region->fx_swaps) > 4)
				region->fx_swap_table = stashTableCreateWithStringKeys(eaSize(&region->fx_swaps), StashDefault);
			FOR_EACH_IN_EARRAY(region->fx_swaps, WorldRegionFXSwap, pFXSwap)
			{
				const DynFxInfo* pOldInfo = GET_REF(pFXSwap->hOldFx);
				const DynFxInfo* pNewInfo = GET_REF(pFXSwap->hNewFx);
				if (!pOldInfo)
				{
					ErrorFilenamef(zmapGetFilename(zmap), "Unknown FXSwap FX %s in region %s", REF_STRING_FROM_HANDLE(pFXSwap->hOldFx), region->name?region->name:"Default");
					continue;
				}
				if (!pNewInfo)
				{
					ErrorFilenamef(zmapGetFilename(zmap), "Unknown FXSwap FX %s in region %s", REF_STRING_FROM_HANDLE(pFXSwap->hNewFx), region->name?region->name:"Default");
					continue;
				}
				if (region->fx_swap_table)
					stashAddPointer(region->fx_swap_table, REF_STRING_FROM_HANDLE(pFXSwap->hOldFx), REF_STRING_FROM_HANDLE(pFXSwap->hNewFx), true);
			}
			FOR_EACH_END;
		}
	}

	if(resNamespaceIsUGC(zmapInfoGetPublicName(zminfo)))
		zmap->isUGCGeneratedMap = true;

	PERFINFO_AUTO_STOP();

	return zmap;
}

bool zmapSaveLayersEx(ZoneMap *zmap, const char *filename, bool force, bool asynchronous, bool skipReferenceLayers)
{
	int i;
	char dirName[MAX_PATH];
	bool ret = true;

	if (filename)
	{
		char *s;
		strcpy(dirName, filename);
		s = strrchr(dirName, '/');
		if (s)
			*(s+1) = 0;
	}

	for (i = 0; i < eaSize(&zmap->layers); i++)
	{
		ZoneMapLayer *layer = zmap->layers[i];
		if(zmap->map_info.genesis_data && !layer->scratch)
			continue;
		layer->genesis = false;
		if (filename)
		{
			char tempName[MAX_PATH];

			if(skipReferenceLayers && layerIsReference(layer))
				continue;

			// Load source if not currently loaded
			if (layer->layer_mode < LAYER_MODE_EDITABLE)
			{
				layerSetMode(layer, LAYER_MODE_TERRAIN, false, false, false);
				layer->layer_mode = LAYER_MODE_EDITABLE;
			}

			strcpy(tempName, dirName);
			strcat(tempName, strrchr(layerGetFilename(layer), '/') + 1);
			layerChangeFilename(layer, tempName);
			zmap->map_info.layers[i]->filename = layer->filename;
		}

		if(!layerSave(layer, force, asynchronous)) {
			Errorf("Failed to Save Layer: %s", layer->filename);
			ret = false;
		}
	}
	return ret;
}

bool zmapIsSaving(ZoneMap *zmap)
{
	int i;
	VALIDATE_ZMAP_RET(zmap, 0);
	for (i = 0; i < eaSize(&zmap->layers); i++)
		if (layerIsSaving(zmap->layers[i]))
			return true;
	return false;
}

bool zmapOrLayersUnsaved(ZoneMap *zmap)
{
	int i;
	VALIDATE_ZMAP_RET(zmap, 0);
	if(zmapInfoGetUnsaved(NULL))
		return true;
	for (i = 0; i < eaSize(&zmap->layers); i++) {
		if (layerGetUnsaved(zmap->layers[i]))
			return true;
	}
	return false;
}

bool zmapCheckFailedValidation(ZoneMap *zmap)
{
	bool failed;
	VALIDATE_ZMAP_RET(zmap, 0);
	failed = zmap->failed_validation;
	zmap->failed_validation = false;
	return failed;
}

void zmapGetOffset(ZoneMap *zmap, Vec3 offset)
{
	int idx;

	zeroVec3(offset);
	VALIDATE_ZMAP(zmap);

	idx = eaFind(&world_grid.maps, zmap);
	if (idx < 0)
		return;

	copyVec3(&world_grid.map_offsets[idx*3], offset);
}

WorldRegion *zmapGetWorldRegionByNameEx(ZoneMap *zmap, const char *name, bool create_if_null)
{
	WorldRegion *region = NULL;
	int i;

	if (name && (name[0] == 0 || stricmp(name, "default")==0))
		name = NULL;

	VALIDATE_ZMAP_RET(zmap, worldGetTempWorldRegionByName(name));

	if (!name)
	{
		// return default region
		if (!eaSize(&zmap->map_info.regions))
			createWorldRegion(zmap, NULL);
		assert(eaSize(&zmap->map_info.regions));
		assert(zmap->map_info.regions[0]->zmap_parent == zmap);
		return zmap->map_info.regions[0];
	}

	name = allocAddString(name);
	for (i = 0; i < eaSize(&zmap->map_info.regions); ++i)
	{
		if (zmap->map_info.regions[i]->name == name)
		{
			region = zmap->map_info.regions[i];
			break;
		}
	}

	if (!region)
	{
		if (!create_if_null)
			return NULL;
		region = createWorldRegion(zmap, name);
	}

	assert(region->zmap_parent == zmap);
	return region;
}

void zmapUnload(ZoneMap *zmap)
{
	int i;

	assert(zmap->zmap_scope);

	eaForEach(&zmap->layers, layerUnload);
	eaForEach(&zmap->layers, layerClear);

	// remove all collision and drawables
	for (i = eaSize(&zmap->map_info.regions) - 1; i >= 0; --i)
	{
		uninitWorldRegion(zmap->map_info.regions[i]);
		if (zmap->map_info.regions[i]->is_editor_region)
		{
			StructDestroy(parse_WorldRegion, zmap->map_info.regions[i]);
			eaRemove(&zmap->map_info.regions, i);
		}
	}

	// destroy scope
	worldZoneMapScopeDestroy(zmap->zmap_scope);
	zmap->zmap_scope = NULL;

	StructDestroySafe(parse_BinFileListWithCRCs, &zmap->external_dependencies);

	worldCellEntryReset(zmap);

	StructDeInit(parse_ZoneMapInfo, &zmap->map_info);
	StructDestroySafe(parse_ZoneMapInfo, &zmap->last_saved_info);

	SAFE_FREE(zmap);
}

//////////////////////////////////////////////////////////////////////////

bool zmapLocked(ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, false);

	return (zmap->map_info.genesis_data == NULL) && zmapInfoLocked(&zmap->map_info);
}

bool zmapGenesisDataLocked(ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, false);

	return (zmap->map_info.genesis_data != NULL) && zmapInfoLocked(&zmap->map_info);
}

//////////////////////////////////////////////////////////////////////////

void zmapUpdateBounds(ZoneMap *zmap)
{
	int i;
	VALIDATE_ZMAP(zmap);
	for (i = 0; i < eaSize(&zmap->layers); ++i)
	{
		if (zmap->layers[i])
			layerUpdateBounds(zmap->layers[i]);
	}
}

void zmapRecalcBounds(ZoneMap *zmap, int close_trackers)
{
	int		i,k;

	VALIDATE_ZMAP(zmap);

	for (k = 0; k < eaSize(&zmap->layers); k++)
		if (zmap->layers[k]->grouptree.def_lib)
		{
			GroupDef **lib_defs = groupLibGetDefEArray(zmap->layers[k]->grouptree.def_lib);

			for (i = 0; i < eaSize(&lib_defs); i++)
			{
				{
					GroupDef *def = lib_defs[i];
					if (def)
					{
						def->bounds_valid = 0;
					}
				}
			}
		}

	zmapUpdateBounds(zmap);

	if (close_trackers)
	{
		for (k = 0; k < eaSize(&zmap->layers); k++)
			layerTrackerClose(zmap->layers[k]);
	}
}

void zmapGetBounds(ZoneMap *zmap, Vec3 world_min, Vec3 world_max)
{
	int i;

	setVec3same(world_min, 8e16);
	setVec3same(world_max, -8e16);

	VALIDATE_ZMAP(zmap);

	for (i = 0; i < eaSize(&zmap->layers); ++i)
	{
		Vec3 layer_min, layer_max;

		layerGetBounds(zmap->layers[i], layer_min, layer_max);
		if (layer_min[0] <= layer_max[0] && layer_min[1] <= layer_max[1] && layer_min[2] <= layer_max[2])
		{
			MINVEC3(world_min, layer_min, world_min);
			MAXVEC3(world_max, layer_max, world_max);
		}

		layerGetTerrainBounds(zmap->layers[i], layer_min, layer_max);
		if (layer_min[0] <= layer_max[0] && layer_min[1] <= layer_max[1] && layer_min[2] <= layer_max[2])
		{
			MINVEC3(world_min, layer_min, world_min);
			MAXVEC3(world_max, layer_max, world_max);
		}
	}
}

void zmapTrackerUpdate(ZoneMap *zmap,bool force,bool update_terrain)
{
	int k;
	VALIDATE_ZMAP(zmap);

	for (k = 0; k < eaSize(&zmap->layers); k++)
	{
		if (zmap->layers[k])
		{
			layerTrackerUpdate(zmap->layers[k], force, false);
		}
	}
	roomConnGraphUpdateAllRegions(zmap);
	zmap->tracker_update_time = worldGetModTime();
}

//////////////////////////////////////////////////////////////////////////

void zmapCommitGenesisData(ZoneMap *zmap)
{
	int i, j;
	VALIDATE_ZMAP(zmap);
	for (i = 0; i < eaSize(&zmap->layers); i++)
	{
		GroupDef **defs = groupLibGetDefEArray(zmap->layers[i]->grouptree.def_lib);
		ZoneMapLayerInfo *layer_info = StructCreate(parse_ZoneMapLayerInfo);

		// Copy the layer to the map info
		layer_info->filename = allocAddFilename(zmap->layers[i]->filename);
		layer_info->region_name = allocAddString(zmap->layers[i]->region_name);
		eaPush(&zmap->map_info.layers, layer_info);

		for ( j=0; j < eaSize(&defs); j++ ) {
			defs[j]->filename = layer_info->filename;
		}

		// Set each layer unsaved, locked, and editable so we can save them
		zmap->layers[i]->target_mode = zmap->layers[i]->layer_mode = LAYER_MODE_EDITABLE;
		zmap->layers[i]->locked = 3;
		zmap->layers[i]->grouptree.unsaved_changes = true;
	}
}

//////////////////////////////////////////////////////////////////////////

ZoneMapLayer *zmapNewLayer(ZoneMap *zmap, int layer_idx, const char *layer_filename)
{
	VALIDATE_ZMAP_RET(zmap, NULL);

	if (layer_idx >= eaSize(&zmap->layers))
		eaSetSize(&zmap->layers, layer_idx + 1);
	assert(zmap->layers && !zmap->layers[layer_idx]);

	zmap->layers[layer_idx] = layerNew(zmap, layer_filename);
	return zmap->layers[layer_idx];
}

ZoneMapLayer *zmapAddLayer(ZoneMap *zmap, const char *layer_filename, const char *layer_name, const char *region_name)
{
	ZoneMapLayer *layer;

	VALIDATE_ZMAP_RET(zmap, NULL);

	zmapInfoAddLayer(&zmap->map_info, layer_filename, region_name);

	layer = zmapNewLayer(zmap, eaSize(&zmap->layers), layer_filename);
	if (!layer)
		return NULL;

	if (wl_state.layer_mode_callback)
		wl_state.layer_mode_callback(layer, LAYER_MODE_EDITABLE, false);

	layer->region_name = region_name;

	worldCellSetEditable();
	layerLoadGroupSource(layer, zmap, layer_name, false);

	layer->target_mode = LAYER_MODE_EDITABLE;

	if (wl_state.layer_mode_callback)
		wl_state.layer_mode_callback(layer, LAYER_MODE_EDITABLE, true);

	layer->layer_mode = LAYER_MODE_EDITABLE;
	layer->locked = 3;
	worldUpdateBounds(false, false);

	return layer;
}

void zmapRemoveLayer(ZoneMap *zmap, int layer_idx)
{
	ZoneMapLayer *layer;
	WorldRegion *region;

	VALIDATE_ZMAP(zmap);

	if (!zmap->layers || layer_idx < 0 || layer_idx >= eaSize(&zmap->layers))
		return;

	worldCellSetEditable();

	layer = zmap->layers[layer_idx];

	region = layerGetWorldRegion(layer);

	layerFree(layer);

	eaRemove(&zmap->layers, layer_idx);
	worldUpdateBounds(false, false);

	if (!zmap->map_info.genesis_data)
		zmapInfoRemoveLayer(&zmap->map_info, layer_idx);

	zmap->deleted_layer = true;
}

void zmapSetLayerRegion(ZoneMap *zmap, ZoneMapLayer *layer, const char *region_name)
{
	int i;
	VALIDATE_ZMAP(zmap);

	for (i = 0; i < eaSize(&zmap->layers); i++)
		if (zmap->layers[i] == layer)
		{
			zmapInfoSetLayerRegion(&zmap->map_info, i, region_name);
		}
}

int zmapGetLayerCount(SA_PARAM_OP_VALID ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, 0);
	return eaSize(&zmap->layers);
}

ZoneMapLayer *zmapGetLayer(ZoneMap *zmap, int layer_idx)
{
	VALIDATE_ZMAP_RET(zmap, NULL);
	if (!zmap->layers || layer_idx < 0 || layer_idx >= eaSize(&zmap->layers))
		return NULL;
	return zmap->layers[layer_idx];
}

ZoneMapLayer *zmapGetLayerByName(ZoneMap *zmap, const char *layer_name)
{
	int i;
	char ns[MAX_PATH], base_name[MAX_PATH], layer_filename[MAX_PATH];

	VALIDATE_ZMAP_RET(zmap, NULL);
	if (!zmap->layers || !layer_name)
		return NULL;

	if(resExtractNameSpace(layer_name, ns, base_name))
		sprintf(layer_filename, NAMESPACE_PATH"%s/%s", ns, base_name);
	else
		strcpy(layer_filename, layer_name);

	for (i = 0; i < eaSize(&zmap->layers); ++i)
	{
		const char *zmap_layer_filename = layerGetFilename(zmap->layers[i]);
		if (zmap->layers[i] && (stricmp(zmap_layer_filename, layer_filename) == 0 || stricmp(zmap_layer_filename, layer_name) == 0))
			return zmap->layers[i];
	}

	return NULL;
}

GroupTracker *zmapGetLayerTracker(ZoneMap *zmap, int layer_idx)
{
	VALIDATE_ZMAP_RET(zmap, NULL);
	if (!zmap->layers || layer_idx < 0 || layer_idx >= eaSize(&zmap->layers))
		return NULL;
	return layerGetTracker(zmap->layers[layer_idx]);
}

// *****************
// Variables
// *****************

bool zmapIsUGCGeneratedMap(ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, false);

	return zmap->isUGCGeneratedMap;
}

const char *zmapGetDefaultVariableMsgKey(ZoneMap *zmap, int var_idx)
{
	const char *filename;
	VALIDATE_ZMAP_RET(zmap, NULL);

	filename = zmapGetFilename(zmap);
	if (filename)
	{
		char key[MAX_PATH];
		char buf[128];

		changeFileExt(filename, "", key);
		strchrReplace(key, '.', '_');
		strchrReplace(key, '/', '_');
		strchrReplace(key, '\\', '_');
		strcat(key, ".Variable.");
		sprintf(buf, "%d", var_idx);
		strcat(key, buf);
		return allocAddString(key);
	}

	return NULL;
}

void zmapRegionSetOverrideCubeMap(ZoneMap *zmap, WorldRegion *region, const char *override_cubemap)
{
	int i;
	VALIDATE_ZMAP(zmap);

	region->override_cubemap = override_cubemap;

	if (region->zmap_parent == zmap)
	{
		for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
		{
			if (region == zmap->map_info.regions[i])
			{
				zmapInfoSetRegionOverrideCubemap(&zmap->map_info, i, override_cubemap);
			}
		}
	}
}

void zmapRegionSetAllowedPetsPerPlayer(ZoneMap *zmap, WorldRegion *region, S32 iAllowedPetsPerPlayer)
{
	int i;
	VALIDATE_ZMAP(zmap);

	if ( iAllowedPetsPerPlayer == 0 )
	{
		region->pRegionRulesOverride.iAllowedPetsPerPlayer = 0;
	}
	else
	{
		region->pRegionRulesOverride.iAllowedPetsPerPlayer = iAllowedPetsPerPlayer;
	}

	if (region->zmap_parent == zmap)
	{
		for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
		{
			if (region == zmap->map_info.regions[i])
			{
				zmapInfoSetRegionAllowedPetsPerPlayer(&zmap->map_info, i, iAllowedPetsPerPlayer);
			}
		}
	}
}

void zmapRegionSetUnteamedPetsPerPlayer(ZoneMap *zmap, WorldRegion *region, S32 iUnteamedPetsPerPlayer)
{
	int i;
	VALIDATE_ZMAP(zmap);

	if ( iUnteamedPetsPerPlayer == 0 )
	{
		region->pRegionRulesOverride.iUnteamedPetsPerPlayer = 0;
	}
	else
	{
		region->pRegionRulesOverride.iUnteamedPetsPerPlayer = iUnteamedPetsPerPlayer;
	}

	if (region->zmap_parent == zmap)
	{
		for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
		{
			if (region == zmap->map_info.regions[i])
			{
				zmapInfoSetRegionUnteamedPetsPerPlayer(&zmap->map_info, i, iUnteamedPetsPerPlayer);
			}
		}
	}
}

void zmapRegionSetVehicleRules(ZoneMap *zmap, WorldRegion *region, S32 eVechicleRules)
{
	int i;
	VALIDATE_ZMAP(zmap);

	region->pRegionRulesOverride.eVehicleRules = eVechicleRules;

	if (region->zmap_parent == zmap)
	{
		for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
		{
			if (region == zmap->map_info.regions[i])
			{
				zmapInfoSetRegionVechicleRules(&zmap->map_info, i, eVechicleRules);
			}
		}
	}
}

void zmapRegionSetSkyGroup(ZoneMap *zmap, WorldRegion *region, SkyInfoGroup *sky_group)
{
	int i;
	VALIDATE_ZMAP(zmap);

	if (region->sky_group)
	{
		if (wl_state.notify_sky_group_freed_func)
			wl_state.notify_sky_group_freed_func(region->sky_group);
		StructDestroy(parse_SkyInfoGroup, region->sky_group);
	}
	region->sky_group = sky_group;

	if (region->zmap_parent == zmap)
	{
		for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
		{
			if (region == zmap->map_info.regions[i])
			{
				zmapInfoSetRegionSkyGroup(&zmap->map_info, i, sky_group);
			}
		}
	}
}

void zmapRegionSetType(ZoneMap *zmap, WorldRegion *region, WorldRegionType type)
{
	int i;
	VALIDATE_ZMAP(zmap);

	worldRegionSetType(region, type);

	if (region->zmap_parent == zmap)
	{
		for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
		{
			if (region == zmap->map_info.regions[i])
			{
				zmapInfoSetRegionType(&zmap->map_info, i, type);
			}
		}
	}
}

void zmapRegionSetWorldGeoClustering(SA_PARAM_OP_VALID ZoneMap *zmap, WorldRegion *region, bool bWorldGeoClustering)
{
	int i;
	VALIDATE_ZMAP(zmap);

	worldRegionSetWorldGeoClustering(region, bWorldGeoClustering);

	if (region->zmap_parent == zmap)
	{
		for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
		{
			if (region == zmap->map_info.regions[i])
			{
				zmapInfoSetRegionWorldGeoClustering(&zmap->map_info, i, bWorldGeoClustering);
			}
		}
	}
}

void zmapRegionSetIndoorLighting(ZoneMap *zmap, WorldRegion *region, bool bIndoorLighting)
{
	int i;
	VALIDATE_ZMAP(zmap);

	worldRegionSetIndoorLighting(region, bIndoorLighting);

	if (region->zmap_parent == zmap)
	{
		for (i = 0; i < eaSize(&zmap->map_info.regions); i++)
		{
			if (region == zmap->map_info.regions[i])
			{
				zmapInfoSetRegionIndoorLighting(&zmap->map_info, i, bIndoorLighting);
			}
		}
	}
}

// Scope data
WorldZoneMapScope *zmapGetScope(ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, NULL);

	return zmap->zmap_scope;
}


GenesisEditType zmapGetGenesisEditType(SA_PARAM_OP_VALID ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, 0);

	return zmap->genesis_edit_type;
}

void zmapSetGenesisEditType(SA_PARAM_OP_VALID ZoneMap *zmap, GenesisEditType type)
{
	VALIDATE_ZMAP(zmap);

	zmap->genesis_edit_type = type;
}

void zmapSetGenesisViewType(SA_PARAM_OP_VALID ZoneMap *zmap, GenesisViewType type)
{
	VALIDATE_ZMAP(zmap);

	zmap->genesis_view_type = type;
}

BinFileListWithCRCs *zmapGetExternalDepsList(SA_PARAM_OP_VALID ZoneMap *zmap)
{
	VALIDATE_ZMAP_RET(zmap, NULL);

	if (!zmap->external_dependencies)
	{
		zmap->external_dependencies = StructCreate(parse_BinFileListWithCRCs);
		zmap->external_dependencies->world_crc = getWorldCellParseTableCRC(false);
		zmap->external_dependencies->file_list = StructCreate(parse_BinFileList);
	}
	return zmap->external_dependencies;
}

void zmapInfoReport(FILE * report_file)
{
	RefDictIterator iterator;
	ReferenceData pRefData;

	RefSystem_InitRefDictIterator(g_ZoneMapDictionary, &iterator);
	while ((pRefData = RefSystem_GetNextReferenceDataFromIterator(&iterator)))
	{
		const ZoneMapInfo * pZMI = (const ZoneMapInfo *)RefSystem_ReferentFromString(g_ZoneMapDictionary, (const char*)pRefData);
		WorldRegion ** zone_regions = pZMI->regions;
		int region_index;
		for (region_index = 0; region_index < eaSize(&zone_regions); ++region_index)
		{
			WorldRegion * region = zone_regions[region_index];
			if (region->type == WRT_Space)
			{
				if (region->bUseIndoorLighting)
					fprintf(report_file, "ZM %s Region %s\n", pZMI->filename, region->name);
			}
		}
	}
}
