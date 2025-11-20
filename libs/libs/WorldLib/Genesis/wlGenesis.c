#define GENESIS_ALLOW_OLD_HEADERS
#include "wlGenesis.h"

#include "EString.h"
#include "Expression.h"
#include "FolderCache.h"
#include "ObjectLibrary.h"
#include "ResourceSearch.h"
#include "ScratchStack.h"
#include "StateMachine.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "UGCProjectUtils.h"
#include "WorldCellStreaming.h"
#include "WorldGrid.h"
#include "WorldGridLoadPrivate.h"
#include "WorldGridPrivate.h"
#include "error.h"
#include "fileutil.h"
#include "gimmeDLLWrapper.h"
#include "hoglib.h"
#include "rand.h"
#include "timing.h"
#include "utilitiesLib.h"
#include "wlExclusionGrid.h"
#include "wlGenesisExterior.h"
#include "wlGenesisExteriorDesign.h"
#include "wlGenesisInterior.h"
#include "wlGenesisMissions.h"
#include "wlGenesisMissions.h"
#include "wlGenesisPopulate.h"
#include "wlGenesisRoom.h"
#include "wlGenesisSolarSystem.h"
#include "wlSky.h"
#include "wlState.h"
#include "wlTerrainPrivate.h"
#include "wlTerrainSource.h"
#include "wlUGC.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

#define SOLAR_SYSTEM_VERSION 2
#define SOLAR_SYSTEM_VERSION_NAME "SolarSystemVersion"
#define GENESIS_INTERIOR_VERSION 2
#define GENESIS_INTERIOR_VERSION_NAME "GenesisInteriorVersion"
#define GENESIS_EXTERIOR_VERSION 2
#define GENESIS_EXTERIOR_VERSION_NAME "GenesisExteriorVersion"
#define GENESIS_UGC_SPACE_VERSION 1
#define GENESIS_UGC_SPACE_VERSION_NAME "GenesisUGCSpaceVersion"

static void* genesisGetRandomItemFromTagList(MersenneTable *table, char *dictionary, char **tag_list, char **append_tag_list);
static void* genesisGetRandomItemFromTagList1(MersenneTable *table, char *dictionary, char **tag_list, char *append_tag);
static const char* genesisGetLayerNameFromType(GenesisMapType type, int layer_idx);
	
#ifndef NO_EDITORS

#define TMOG_SOLAR_SYSTEM_VERSION	1 // Last change: N/A
#define TMOG_INTERIOR_VERSION		0 // Last change: N/A
#define TMOG_EXTERIOR_VERSION		0 // Last change: N/A
#define TMOG_UGC_INTERIOR_VERSION	0 // Last change: N/A
#define TMOG_UGC_SPACE_VERSION		0 // Last change: N/A

#define GENESIS_MIN_PATH_LENGTH		2 // Shortest path we allow

static DictionaryHandle genesis_ext_layout_temp_dict = NULL;
static DictionaryHandle genesis_int_layout_temp_dict = NULL;

static DictionaryHandle genesis_backdrop_dict = NULL;
DictionaryHandle g_MapDescDictionary = NULL;
DictionaryHandle g_EpisodeDictionary = NULL;

static bool gGenesisUnfreezeDisabled = false;

typedef void (*genesisMissionTransChallengeFunc)(void *userdata, char *room_name, const char *object_name, GenesisMissionChallenge *challenge, int *current_id, int count, GenesisMapDescription *map_desc, int mission_uid, bool exterior, GenesisEncounterJitter *jitter);
typedef void (*genesisMissionTransPortalFunc)(void *userdata, GenesisMissionPortal *portal, GenesisMapDescription *map_desc, int mission_uid);

static bool genesisMapDescriptionValidate(GenesisMapDescription *map_desc);
static int genesisExteriorLayoutTemplateValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GenesisMapDescExteriorLayoutTemplate *ext_template, U32 userID);
static int genesisInteriorLayoutTemplateValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GenesisMapDescInteriorLayoutTemplate *int_template, U32 userID);

//////////////////////////////////////////////////////////////////
// Exterior Layout Template Library
//////////////////////////////////////////////////////////////////

//Fills in the name of the GenesisMapDescExteriorLayoutTemplate during load
AUTO_FIXUPFUNC;
TextParserResult genesisInitExteriorLayoutTemplateFixup(GenesisMapDescExteriorLayoutTemplate *ext_template, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char name[256];

			StructFreeStringSafe(&ext_template->name);
			getFileNameNoExt(name, ext_template->filename);
			ext_template->name = StructAllocString(name);
		}
	}
	return 1;
}

AUTO_RUN;
void genesisInitExteriorLayoutTemplateLib(void)
{
	genesis_ext_layout_temp_dict = RefSystem_RegisterSelfDefiningDictionary( GENESIS_EXT_LAYOUT_TEMP_FILE_DICTIONARY, false, parse_GenesisMapDescExteriorLayoutTemplate, true, false, NULL );
	resDictManageValidation(genesis_ext_layout_temp_dict, genesisExteriorLayoutTemplateValidateCB);

	if (isDevelopmentMode() || isProductionEditMode()) {
		resDictMaintainInfoIndex(genesis_ext_layout_temp_dict, NULL, NULL, NULL, NULL, NULL);
	}
}

//Reload Exterior Layout Template File
static void genesisReloadExteriorLayoutTemplate(const char *relpath, int UNUSED_when)
{
	loadstart_printf( "Reloading Exterior Layout Template Files..." );
	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, genesis_ext_layout_temp_dict );
	loadend_printf( "done" );
}

//Init Exterior Layout Template Lib
void genesisLoadExteriorLayoutTemplateLib()
{
	static bool templates_loaded = false;
	if(templates_loaded || !areEditorsPossible())
		return;
	resLoadResourcesFromDisk(genesis_ext_layout_temp_dict, "genesis/layout_templates/exteriors", ".extlt", "GenesisExteriorLayoutTemplate.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "genesis/layout_templates/exteriors/*.extlt", genesisReloadExteriorLayoutTemplate );
	templates_loaded = true;
}

//////////////////////////////////////////////////////////////////
// Interior Layout Template Library
//////////////////////////////////////////////////////////////////

//Fills in the name of the GenesisMapDescInteriorLayoutTemplate during load
AUTO_FIXUPFUNC;
TextParserResult genesisInitInteriorLayoutTemplateFixup(GenesisMapDescInteriorLayoutTemplate *int_template, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char name[256];

			StructFreeStringSafe(&int_template->name);
			getFileNameNoExt(name, int_template->filename);
			int_template->name = StructAllocString(name);
		}
	}
	return 1;
}

AUTO_RUN;
void genesisInitInteriorLayoutTemplateLib(void)
{
	genesis_int_layout_temp_dict = RefSystem_RegisterSelfDefiningDictionary( GENESIS_INT_LAYOUT_TEMP_FILE_DICTIONARY, false, parse_GenesisMapDescInteriorLayoutTemplate, true, false, NULL );
	resDictManageValidation(genesis_int_layout_temp_dict, genesisInteriorLayoutTemplateValidateCB);
	if (isDevelopmentMode() || isProductionEditMode()) {
		resDictMaintainInfoIndex(genesis_int_layout_temp_dict, NULL, NULL, NULL, NULL, NULL);
	}
}

//Reload Interior Layout Template File
static void genesisReloadInteriorLayoutTemplate(const char *relpath, int UNUSED_when)
{
	loadstart_printf( "Reloading Interior Layout Template Files..." );
	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, genesis_int_layout_temp_dict );
	loadend_printf( "done" );
}

//Init Interior Layout Template Lib
void genesisLoadInteriorLayoutTemplateLib()
{
	static bool templates_loaded = false;
	if(templates_loaded || !areEditorsPossible())
		return;
	resLoadResourcesFromDisk(genesis_int_layout_temp_dict, "genesis/layout_templates/interiors", ".intlt", "GenesisInteriorLayoutTemplate.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "genesis/layout_templates/interiors/*.intlt", genesisReloadInteriorLayoutTemplate );
	templates_loaded = true;
}

//////////////////////////////////////////////////////////////////
// Backdrop Library
//////////////////////////////////////////////////////////////////

//Fills in the name of the GenesisBackdrop during load
AUTO_FIXUPFUNC;
TextParserResult solarSystemSpaceBackdropFixup(GenesisBackdrop *backdrop, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
	case FIXUPTYPE_POST_TEXT_READ:
		{
			char name[256];

			StructFreeStringSafe(&backdrop->name);
			if (backdrop->filename)
			{
				getFileNameNoExt(name, backdrop->filename);
				backdrop->name = StructAllocString(name);
			}
		}
	}
	return 1;
}

AUTO_RUN;
void solarSystemInitSpaceBackdropLib(void)
{
	genesis_backdrop_dict = RefSystem_RegisterSelfDefiningDictionary( GENESIS_BACKDROP_FILE_DICTIONARY, false, parse_GenesisBackdrop, true, false, NULL );
	resDictMaintainInfoIndex(genesis_backdrop_dict, NULL, NULL, ".Tags", NULL, NULL);
}

//Reload GenesisBackdrop File
static void solarSystemReloadSpaceBackdrop(const char *relpath, int UNUSED_when)
{
	loadstart_printf( "Reloading GenesisBackdrop Files..." );
	fileWaitForExclusiveAccess( relpath );
	ParserReloadFileToDictionary( relpath, genesis_backdrop_dict );
	loadend_printf( "done" );
}

//Init GenesisBackdrop Lib
void genesisLoadBackdropLib()
{
	static bool backdrops_loaded = false;
	if(backdrops_loaded || !areEditorsPossible())
		return;
	resLoadResourcesFromDisk(genesis_backdrop_dict, "genesis/backdrops", ".backdrop", "GenesisBackdrops.bin", RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	FolderCacheSetCallback( FOLDER_CACHE_CALLBACK_UPDATE, "genesis/backdrops/*.backdrop", solarSystemReloadSpaceBackdrop );
	backdrops_loaded = true;
}

void genesisLoadAllLibraries()
{
	if(!objectLibraryLoaded())
		return;

	// Genesis Common
	genesisLoadBackdropLib();

	// Interior & Exterior
	genesisLoadRoomDefLibrary();
	genesisLoadDetailKitLibrary();
	
	// Interiors only
	genesisLoadInteriorKitLibrary();
	genesisLoadInteriorLayoutTemplateLib();

	// Exteriors only
	genesisLoadGeotypeLibrary();
	genesisLoadEcotypeLibrary();
	genesisLoadExteriorLayoutTemplateLib();
}

static WorldVariableDef* genesisInternVariableDef( ZoneMapInfo *zminfo, WorldVariableDef *varDef)
{
	int i;
	for( i = eaSize(&zminfo->variable_defs) - 1; i >= 0; --i ) {
		if( zminfo->variable_defs[i]->pcName == varDef->pcName ) {
			return zminfo->variable_defs[i];
		}
	}

	{
		WorldVariableDef* newVarDef = StructClone( parse_WorldVariableDef, varDef );
		eaPush(&zminfo->variable_defs, newVarDef);
		
		return newVarDef;
	}
}


/// Ensure that ZMINFO has all variables a genesis map should have.
///
/// If REMOVE-OTHER-VARS is true, then also remove any non-genesis map
/// vars.
///
/// This should be kept in sync with genesisVariableDefNames()
void genesisInternVariableDefs(ZoneMapInfo* zminfo, bool removeOtherVars)
{
	bool isStarClusterMap = genesisIsStarClusterMap(zminfo);
	bool isUGCGeneratedMap = resNamespaceIsUGC(zmapInfoGetCurrentName(zminfo));

	static WorldVariableDef* missionNumDef = NULL;
	static WorldVariableDef* mapEntryKeyDef = NULL;
	if( !missionNumDef ) {
		missionNumDef = StructCreate( parse_WorldVariableDef );
		
		missionNumDef->pcName = allocAddString( "Mission_Num" );
		missionNumDef->eType = WVAR_INT;
		missionNumDef->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
		missionNumDef->pSpecificValue = StructAlloc( parse_WorldVariable );
		missionNumDef->pSpecificValue->pcName = missionNumDef->pcName;
		missionNumDef->pSpecificValue->eType = WVAR_INT;
		missionNumDef->pSpecificValue->iIntVal = 0;
	}
	if( !mapEntryKeyDef ) {
		mapEntryKeyDef = StructCreate( parse_WorldVariableDef );

		mapEntryKeyDef->pcName = allocAddString( "MAP_ENTRY_KEY" );
		mapEntryKeyDef->eType = WVAR_STRING;
		mapEntryKeyDef->eDefaultType = WVARDEF_SPECIFY_DEFAULT;
		mapEntryKeyDef->pSpecificValue = StructAlloc( parse_WorldVariable );
		mapEntryKeyDef->pSpecificValue->pcName = mapEntryKeyDef->pcName;
		mapEntryKeyDef->pSpecificValue->eType = WVAR_STRING;
		mapEntryKeyDef->pSpecificValue->pcStringVal = StructAllocString( "" );
	}

	genesisInternVariableDef( zminfo, missionNumDef );

	if( isStarClusterMap ) {
		genesisInternVariableDef( zminfo, mapEntryKeyDef );
	}

	{
		WorldVariableDef** defs = NULL;
		int it;
			
		GenesisConfig* config = genesisConfig();
		if( config ) {
			defs = config->variable_defs;
		}

		for( it = 0; it != eaSize( &defs ); ++it ) {
			genesisInternVariableDef( zminfo, defs[ it ]);
		}

		// remove other vars
		if( removeOtherVars ) {
			int defaultIt;
			for( it = eaSize( &zminfo->variable_defs ) - 1; it >= 0; --it ) {
				WorldVariableDef* varDef = zminfo->variable_defs[ it ];
				bool found = false;

				if( defs ) {
					for( defaultIt = 0; defaultIt != eaSize( &defs ); ++defaultIt ) {
						WorldVariableDef* defaultVarDef = defs[ defaultIt ];

						if( stricmp( varDef->pcName, defaultVarDef->pcName ) == 0 ) {
							found = true;
							break;
						}
					}
				}
				if( stricmp( varDef->pcName, missionNumDef->pcName ) == 0 ) {
					found = true;
				}
				if( isStarClusterMap && stricmp( varDef->pcName, mapEntryKeyDef->pcName ) == 0 ) {
					found = true;
				}

				if( !found ) {
					eaRemove( &zminfo->variable_defs, it );
				}
			}
		}
	}
}

/// Fill OUT-VARIABLE-NAMES with all the variable names for a Genesis
/// map.
///
/// This should be kept in sync with genesisInternVariableDefs()
void genesisVariableDefNames(const char*** outVariableNames)
{
	assert( eaSize( outVariableNames ) == 0 );
	
	eaPush( outVariableNames, allocAddString( "Mission_Num" ));	

	{
		GenesisConfig* config = genesisConfig();
		int it;
		if( config ) {
			for( it = 0; it != eaSize( &config->variable_defs ); ++it ) {
				eaPush( outVariableNames, config->variable_defs[ it ]->pcName );
			}
		}
	}
}


//////////////////////////////////////////////////////////////////
// Error handling
//////////////////////////////////////////////////////////////////

GenesisRuntimeStage *gCurrentStage = NULL;
char* gCurrentMap = NULL;

GenesisRuntimeStage *genesisGetCurrentStage()
{
	return gCurrentStage;
}

bool genesisStageFailed()
{
	return genesisStageHasErrors(GENESIS_FATAL_ERROR);
}

bool genesisStageHasErrors(GenesisRuntimeErrorType errorLevel)
{
	int j;
	if(!gCurrentStage)
		return false;
	for (j = 0; j < eaSize(&gCurrentStage->errors); j++)
		if (gCurrentStage->errors[j]->type >= errorLevel)
			return true;
	return false;
}

bool genesisStatusFailed(GenesisRuntimeStatus *status)
{
	return genesisStatusHasErrors(status, GENESIS_FATAL_ERROR);
}

bool genesisStatusHasErrors(GenesisRuntimeStatus *status, GenesisRuntimeErrorType errorLevel)
{
	int i, j;
	for (i = 0; i < eaSize(&status->stages); i++)
		for (j = 0; j < eaSize(&status->stages[i]->errors); j++)
			if (status->stages[i]->errors[j]->type >= errorLevel)
				return true;
	return false;
}

void genesisSetStage(GenesisRuntimeStage *stage)
{
	gCurrentStage = stage;
}

void genesisSetStageAndAdd(GenesisRuntimeStatus *status, char* stageName, ...)
{
	char stageNameBuffer[ 256 ];
	GenesisRuntimeStage* stage;

	va_list ap;
	va_start(ap, stageName);
	vsprintf(stageNameBuffer, stageName, ap);
	va_end(ap);

	stage = StructCreate( parse_GenesisRuntimeStage );
	stage->name = StructAllocString( stageNameBuffer );
	eaPush(&status->stages, stage);
	gCurrentStage = stage;
}

const GenesisRuntimeError* genesisStatusMostImportantError( const GenesisRuntimeStatus* status )
{
	int stageIt;
	int errorIt;

	const GenesisRuntimeError* highest_priority_error = NULL;
	
	for( stageIt = 0; stageIt != eaSize( &status->stages ); ++stageIt ) {
		for( errorIt = 0; errorIt != eaSize( &status->stages[ stageIt ]->errors ); ++errorIt ) {
			const GenesisRuntimeError* error = status->stages[ stageIt ]->errors[ errorIt ];

			if( !highest_priority_error || error->type > highest_priority_error->type ) {
				highest_priority_error = error;
			}
		}
	}

	return highest_priority_error;
}

void genesisSetMap(const char * mapName)
{
	if (mapName)
		StructCopyString(&gCurrentMap, mapName);
	else
		StructFreeStringSafe(&gCurrentMap);
}

//////////////////////////////////////////////////////////////////
// Procedural object properties
//////////////////////////////////////////////////////////////////

void genesisObjectGetAbsolutePos(GenesisToPlaceObject* obj, Vec3 out)
{
	setVec3same( out, 0 );
 	while( obj ) {
		addVec3( out, obj->mat[3], out );

		if( !obj->mat_relative ) {
			obj = NULL;
		} else {
			obj = obj->parent;
		}
	}
}

void genesisProceduralObjectEnsureType(GenesisProceduralObjectParams *params)
{
	if (!params->hull_properties)
		params->hull_properties = StructCreate(parse_GroupHullProperties);

	if (params->power_volume)
		eaPushUnique(&params->hull_properties->ppcTypes, allocAddString("Power"));
	if (params->fx_volume)
		eaPushUnique(&params->hull_properties->ppcTypes, allocAddString("FX"));
	if (params->sky_volume_properties)
		eaPushUnique(&params->hull_properties->ppcTypes, allocAddString("SkyFade"));
	if (params->event_volume_properties)
		eaPushUnique(&params->hull_properties->ppcTypes, allocAddString("Event"));
	if (params->action_volume_properties)
		eaPushUnique(&params->hull_properties->ppcTypes, allocAddString("Action"));
	if (params->optionalaction_volume_properties)
		eaPushUnique(&params->hull_properties->ppcTypes, allocAddString("OptionalAction"));
}

void genesisProceduralObjectAddVolumeType(GenesisProceduralObjectParams *params, const char *type)
{
	if (!params->hull_properties)
		params->hull_properties = StructCreate(parse_GroupHullProperties);

	type = allocAddString(type);
	eaPushUnique(&params->hull_properties->ppcTypes, type);
}

void genesisProceduralObjectSetEventVolume(GenesisProceduralObjectParams *params)
{
	if (!params->event_volume_properties)
		params->event_volume_properties = StructCreate(parse_WorldEventVolumeProperties);
	
	genesisProceduralObjectEnsureType(params);
}

void genesisProceduralObjectSetActionVolume(GenesisProceduralObjectParams *params)
{
	if (!params->action_volume_properties)
		params->action_volume_properties = StructCreate(parse_WorldActionVolumeProperties);

	genesisProceduralObjectEnsureType(params);
}

void genesisProceduralObjectSetOptionalActionVolume(GenesisProceduralObjectParams *params)
{
	if (!params->optionalaction_volume_properties)
		params->optionalaction_volume_properties = StructCreate(parse_WorldOptionalActionVolumeProperties);

	genesisProceduralObjectEnsureType(params);
}

//////////////////////////////////////////////////////////////////
// Procedural Content support
//////////////////////////////////////////////////////////////////

AUTO_RUN;
void genesisContentRegister(void)
{
	ParserBinRegisterDepValue(SOLAR_SYSTEM_VERSION_NAME, SOLAR_SYSTEM_VERSION);
	ParserBinRegisterDepValue(GENESIS_INTERIOR_VERSION_NAME, GENESIS_INTERIOR_VERSION);
	ParserBinRegisterDepValue(GENESIS_EXTERIOR_VERSION_NAME, GENESIS_EXTERIOR_VERSION);
	ParserBinRegisterDepValue(GENESIS_UGC_SPACE_VERSION_NAME, GENESIS_UGC_SPACE_VERSION);
}

static GenesisInstancePropertiesApplyFn genesis_instance_fn = NULL;

void genesisSetInstancePropertiesFunction(GenesisInstancePropertiesApplyFn function)
{
	genesis_instance_fn = function;
}

/*
void genesisAddGenesisDependencies(ZoneMapInfo *zminfo, TextParserState *state)
{
	if (zmap->map_info.genesis_data->solar_system)
	{
		ParserBinAddValueDep(state, SOLAR_SYSTEM_VERSION_NAME);
	}
	else if (zmap->map_info.genesis_data->genesis_interior)
	{
		ParserBinAddValueDep(state, GENESIS_INTERIOR_VERSION_NAME);
	}
	else if (zmap->map_info.genesis_data->genesis_exterior || zmap->map_info.genesis_data->genesis_exterior_nodes)
	{
		ParserBinAddValueDep(state, GENESIS_EXTERIOR_VERSION_NAME);
	}
}
*/

static bool genesisPrepareLayer(ZoneMapLayer *layer, bool preview_mode)
{
	char gen_bin_filename[MAX_PATH];
	char gen_bin_filename_write[MAX_PATH];

	layerSetMode(layer, LAYER_MODE_GROUPTREE, false, true, false);

	layerGetGenesisDir(gen_bin_filename, ARRAY_SIZE_CHECKED(gen_bin_filename), layer);
	fileLocateWrite(gen_bin_filename, gen_bin_filename_write);
	if (fileExists(gen_bin_filename_write))
	{
		int remove_result = 0;
		if(IsServer() || preview_mode || isProductionEditMode())
		{
			remove_result = remove(gen_bin_filename_write);
			layerUnload(layer);
			layerSetMode(layer, LAYER_MODE_GROUPTREE, false, true, false);
		}
		else //IsClient
		{
			layerUnload(layer);
			return false;
		}
	}
	layer->layer_mode = LAYER_MODE_EDITABLE;
	return true;
}

static void genesisCompleteLayer(ZoneMapLayer *layer, bool write_layer)
{
	char gen_bin_filename[MAX_PATH];
	char gen_bin_filename_write[MAX_PATH];

	layerGetGenesisDir(gen_bin_filename, ARRAY_SIZE_CHECKED(gen_bin_filename), layer);
	fileLocateWrite(gen_bin_filename, gen_bin_filename_write);

	// Create dummy source file
	layerSaveAs(layer, write_layer ? layer->filename : gen_bin_filename_write, true, false, false, write_layer); //< The messages are not
																		   //< stored alongside the
																		   //< layer, since they are
																		   //< saved out seperately.
	binNotifyTouchedOutputFile(gen_bin_filename_write);

	// Create header bin file
	layerGetHeaderBinFile(gen_bin_filename, ARRAY_SIZE_CHECKED(gen_bin_filename), layer);
	fileLocateWrite(gen_bin_filename, gen_bin_filename_write);
	ParserWriteTextFile(gen_bin_filename_write, parse_ZoneMapTerrainLayer, &layer->terrain, 0, 0);

	//layerUnload(layer);
}

static void genesisGenerateGeometryMakeTerrain(ZoneMap *zmap, ZoneMapLayer *layer)
{
	if (eaSize(&layer->terrain.blocks) == 0)
	{
		char buf[64];
		if ((zmap->map_info.genesis_data->genesis_exterior && zmap->map_info.genesis_data->genesis_exterior->is_vista) ||
			(zmap->map_info.genesis_data->genesis_exterior_nodes && zmap->map_info.genesis_data->genesis_exterior_nodes->is_vista))
		{
			TerrainBlockRange *range;
			GenesisConfig* config = genesisConfig();
			int vista_thickness = SAFE_MEMBER(config, vista_thickness);
			int vista_hole_size = SAFE_MEMBER(config, vista_hole_size);
			if(vista_thickness <= 0)
				vista_thickness = GENESIS_EXTERIOR_DEFAULT_VISTA_THICKNESS;
			if(vista_hole_size <= 0)
				vista_hole_size = GENESIS_EXTERIOR_DEFAULT_VISTA_HOLE_SIZE;

			layer->terrain.non_playable = true;
			range = StructCreate(parse_TerrainBlockRange);
			sprintf(buf, "%s_B0", layer->name);
			range->range_name = StructAllocString(buf);
			setVec3(range->range.min_block, -vista_thickness, 0, -vista_thickness);
			setVec3(range->range.max_block, vista_hole_size-1, 0, -1);
			eaPush(&layer->terrain.blocks, range);
			range = StructCreate(parse_TerrainBlockRange);
			sprintf(buf, "%s_B1", layer->name);
			range->range_name = StructAllocString(buf);
			setVec3(range->range.min_block, vista_hole_size, 0, -vista_thickness);
			setVec3(range->range.max_block, vista_hole_size-1+vista_thickness, 0, vista_hole_size-1);
			eaPush(&layer->terrain.blocks, range);
			range = StructCreate(parse_TerrainBlockRange);
			sprintf(buf, "%s_B2", layer->name);
			range->range_name = StructAllocString(buf);
			setVec3(range->range.min_block, -vista_thickness, 0, 0);
			setVec3(range->range.max_block, -1, 0, vista_hole_size-1+vista_thickness);
			eaPush(&layer->terrain.blocks, range);
			range = StructCreate(parse_TerrainBlockRange);
			sprintf(buf, "%s_B3", layer->name);
			range->range_name = StructAllocString(buf);
			setVec3(range->range.min_block, 0, 0, vista_hole_size);
			setVec3(range->range.max_block, vista_hole_size-1+vista_thickness, 0, vista_hole_size-1+vista_thickness);
			eaPush(&layer->terrain.blocks, range);
		}
		else
		{
			IVec2 min_block, max_block;
			TerrainBlockRange *range = StructCreate(parse_TerrainBlockRange);
			sprintf(buf, "%s_B0", layer->name);
			range->range_name = StructAllocString(buf);
			genesisExteriorGetBlockExtents(zmap->map_info.genesis_data, min_block, max_block);
			setVec3(range->range.min_block, min_block[0], 0, min_block[1]);
			setVec3(range->range.max_block, max_block[0], 0, max_block[1]);
			eaPush(&layer->terrain.blocks, range);
		}
	}
}

static ZoneMapLayer* genesisFindLayerByName(ZoneMap *zmap, const char *name)
{
	int i;
	for (i = 0; i < eaSize(&zmap->layers); ++i)
	{
		char layer_name[256];
		const char *layer_filename = layerGetFilename(zmap->layers[i]);
		getFileNameNoExt(layer_name, layer_filename);
		if (stricmp(layer_name, name) == 0)
			return zmap->layers[i];
	}
	return NULL;
}

#define GENESIS_WORLD_BOUNDS 14900.0f
#define GENESIS_OVERLAP_CORRECTION 50.0f
static void genesisGetSolarSystemCenter(Vec3 center, int pos_idx)
{
	center[0] = ((pos_idx%2) ? 1.0f : -1.0f) * GENESIS_WORLD_BOUNDS/2.0f;
	center[2] = (((pos_idx%4)/2) ? 1.0f : -1.0f) * GENESIS_WORLD_BOUNDS/2.0f;
	if(pos_idx >= 8)
		center[1] = 2.0f*GENESIS_WORLD_BOUNDS/3.0f;
	else if(pos_idx >= 4)
		center[1] = -2.0f*GENESIS_WORLD_BOUNDS/3.0f;
}

void genesisGetBoundsForLayer(GenesisZoneMapData *gen_data, GenesisMapType type, int layer_idx, Vec3 bounds_min, Vec3 bounds_max)
{
	Vec3 center = {0,0,0};
	int pos_idx = 0;
	switch(type)
	{
	case GenesisMapType_Exterior:
		bounds_min[0] = -GENESIS_WORLD_BOUNDS/2.0f; 
		bounds_min[1] = -GENESIS_WORLD_BOUNDS/3.0f; 
		bounds_min[2] = -GENESIS_WORLD_BOUNDS/2.0f; 
		bounds_max[0] =  GENESIS_WORLD_BOUNDS/2.0f; 
		bounds_max[1] =  GENESIS_WORLD_BOUNDS/3.0f; 
		bounds_max[2] =  GENESIS_WORLD_BOUNDS/2.0f;
		break;
	case GenesisMapType_SolarSystem:
	case GenesisMapType_UGC_Space:
		//Exteriors take up the first 4 slots for solar systems.
		if(gen_data->genesis_exterior || gen_data->genesis_exterior_nodes)
			pos_idx = 4;
		pos_idx += layer_idx;
		if( pos_idx == 0 && eaSize(&gen_data->solar_systems)==1 && eaSize(&gen_data->genesis_interiors)==0 )
			setVec3same(center, 0);
		else
			genesisGetSolarSystemCenter(center, pos_idx);
		bounds_min[0] = center[0] - GENESIS_WORLD_BOUNDS/2.0f; 
		bounds_min[1] = center[1] - GENESIS_WORLD_BOUNDS/3.0f; 
		bounds_min[2] = center[2] - GENESIS_WORLD_BOUNDS/2.0f; 
		bounds_max[0] = center[0] + GENESIS_WORLD_BOUNDS/2.0f; 
		bounds_max[1] = center[1] + GENESIS_WORLD_BOUNDS/3.0f; 
		bounds_max[2] = center[2] + GENESIS_WORLD_BOUNDS/2.0f;
		break;
	case GenesisMapType_Interior:
		//First find the center of the next solar system
		//Exteriors take up the first 4 slots for solar systems.
		if(gen_data->genesis_exterior || gen_data->genesis_exterior_nodes)
			pos_idx = 4;
		//SolarSystems each take up a slot
		pos_idx += eaSize(&gen_data->solar_systems);
		if( pos_idx == 0 && eaSize(&gen_data->genesis_interiors)==1 )
		{
			setVec3same(center, 0);
		}
		else
		{
			//Each Solar System slot can hold 8 interiors
			//+1 because we want to be place after the last solar system
			genesisGetSolarSystemCenter(center, pos_idx + layer_idx/8 + 1);

			pos_idx = layer_idx;

			center[0] += ((pos_idx%2) ? 1.0f : -1.0f) * GENESIS_WORLD_BOUNDS/4.0f;
			center[2] += (((pos_idx%4)/2) ? 1.0f : -1.0f) * GENESIS_WORLD_BOUNDS/4.0f;
			if(pos_idx%8 >= 4)
				center[1] += GENESIS_WORLD_BOUNDS/6.0f;
			else
				center[1] -= GENESIS_WORLD_BOUNDS/6.0f;
		}

		bounds_min[0] = center[0] - GENESIS_WORLD_BOUNDS/4.0f; 
		bounds_min[1] = center[1] - GENESIS_WORLD_BOUNDS/6.0f; 
		bounds_min[2] = center[2] - GENESIS_WORLD_BOUNDS/4.0f; 
		bounds_max[0] = center[0] + GENESIS_WORLD_BOUNDS/4.0f; 
		bounds_max[1] = center[1] + GENESIS_WORLD_BOUNDS/6.0f; 
		bounds_max[2] = center[2] + GENESIS_WORLD_BOUNDS/4.0f;
		break;
	case GenesisMapType_UGC_Prefab:
		bounds_min[0] = center[0] - GENESIS_WORLD_BOUNDS/2.0f;
		bounds_min[1] = center[1] - GENESIS_WORLD_BOUNDS/2.0f;
		bounds_min[2] = center[2] - GENESIS_WORLD_BOUNDS/2.0f;
		bounds_max[0] = center[0] + GENESIS_WORLD_BOUNDS/2.0f;
		bounds_max[1] = center[1] + GENESIS_WORLD_BOUNDS/2.0f;
		bounds_max[2] = center[2] + GENESIS_WORLD_BOUNDS/2.0f;
		break;
	}
	addVec3same(bounds_min, GENESIS_OVERLAP_CORRECTION, bounds_min);
	subVec3same(bounds_max, GENESIS_OVERLAP_CORRECTION, bounds_max);
}

static void genesisPopulateErrorLayer(ZoneMapInfo *zmap_info, ZoneMapLayer *layer)
{
	GenesisToPlaceState to_place = { 0 };
	GroupDef *def = objectLibraryGetGroupDefByName("UGC_Error_Map", false);
	if (def)
	{
		GenesisToPlaceObject *object = calloc(1, sizeof(GenesisToPlaceObject));
		object->uid = def->name_uid;
		identityMat4(object->mat);
		eaPush(&to_place.objects, object);
		genesisPlaceObjects(zmap_info, &to_place, layer->grouptree.root_def);
	}
	wl_state.genesis_fail_flag = true;
}

void genesisGenerateResetError()
{
	wl_state.genesis_fail_flag = false;
}

bool genesisGeneratedWithErrors()
{
	return wl_state.genesis_fail_flag;
}

void genesisGenerateGeometry(int iPartitionIdx, ZoneMap *zmap, GenesisMissionRequirements** mission_reqs, bool preview_mode, bool write_layers)
{
	int i, layer_idx;
	ZoneMapLayer *layer;
	bool *layers_made;

	if(eaSize(&zmap->layers) <= 0)
		return;

	if( isDevelopmentMode() && !heapValidateAllReturn() ) {
		FatalErrorf( "heapValidateAllReturn() pre-Genesis failed." );
	}
	
	layers_made = ScratchAlloc(sizeof(bool) * eaSize(&zmap->layers));

	for ( i=0; i < eaSize(&zmap->map_info.genesis_data->solar_systems); i++ )
	{
		GenesisSolSysZoneMap *data = zmap->map_info.genesis_data->solar_systems[i];

		solarSystemCalculateShoeboxPositions(zmap, data, i);

		//Main Layer
		layer = genesisFindLayerByName(zmap, genesisGetLayerNameFromType(GenesisMapType_SolarSystem, i));
		assert(layer);

		layer_idx = eaFind(&zmap->layers, layer);
		assert(layer_idx >= 0 && layer_idx < eaSize(&zmap->layers));
		layers_made[layer_idx] = true;

		if (genesisPrepareLayer(layer, preview_mode))
		{
			solarSystemPopulateLayer(zmap, mission_reqs, layer, data, i);
			genesisCompleteLayer(layer, write_layers);
		}

		//Overview Layer
		layer = genesisFindLayerByName(zmap, genesisGetLayerNameFromType(GenesisMapType_MiniSolarSystem, i));
		assert(layer);

		layer_idx = eaFind(&zmap->layers, layer);
		assert(layer_idx >= 0 && layer_idx < eaSize(&zmap->layers));
		layers_made[layer_idx] = true;

		if (genesisPrepareLayer(layer, preview_mode))
		{
			solarSystemPopulateMiniLayer(zmap, mission_reqs, layer, data, i);
			genesisCompleteLayer(layer, write_layers);
		}
	}

	for ( i=0; i < eaSize(&zmap->map_info.genesis_data->genesis_interiors); i++ )
	{
		GenesisZoneInterior *data = zmap->map_info.genesis_data->genesis_interiors[i];
		layer = genesisFindLayerByName(zmap, genesisGetLayerNameFromType(GenesisMapType_Interior, i));
		assert(layer);

		layer_idx = eaFind(&zmap->layers, layer);
		assert(layer_idx >= 0 && layer_idx < eaSize(&zmap->layers));
		layers_made[layer_idx] = true;

		if (genesisPrepareLayer(layer, preview_mode))
		{
			genesisInteriorPopulateLayer(iPartitionIdx, zmapGetInfo(zmap), zmap->genesis_view_type, mission_reqs, layer, data, i);
			if (genesisStageHasErrors(GENESIS_ERROR))
				genesisPopulateErrorLayer(zmapGetInfo(zmap), layer);
			genesisCompleteLayer(layer, write_layers);
		}
	}

	if (zmap->map_info.genesis_data->genesis_exterior || zmap->map_info.genesis_data->genesis_exterior_nodes)
	{
		layer = genesisFindLayerByName(zmap, genesisGetLayerNameFromType(GenesisMapType_Exterior, 0));
		assert(layer);

		layer_idx = eaFind(&zmap->layers, layer);
		assert(layer_idx >= 0 && layer_idx < eaSize(&zmap->layers));
		layers_made[layer_idx] = true;

		if (genesisPrepareLayer(layer, preview_mode))
		{
			genesisGenerateGeometryMakeTerrain(zmap, layer);
			genesisExteriorPopulateLayer(iPartitionIdx, zmap, mission_reqs, layer);
			genesisCompleteLayer(layer, write_layers);
		}
	}

	if (eaSize(&zmap->layers) == 1 && !layers_made[0])
	{
		layer = genesisFindLayerByName(zmap, genesisGetLayerNameFromType(GenesisMapType_Interior, 0));
		assert(layer);

		layers_made[0] = true;

		if (genesisPrepareLayer(layer, preview_mode))
		{
			genesisPopulateErrorLayer(zmapGetInfo(zmap), layer);
			genesisCompleteLayer(layer, write_layers);
		}
	}

	for ( i=0; i < eaSize(&zmap->layers); i++ )
		assert(layers_made[i]);
	ScratchFree(layers_made);

	if( isDevelopmentMode() && !heapValidateAllReturn() ) {
		FatalErrorf( "heapValidateAllReturn() post-Genesis failed." );
	}
}

GenesisZoneNodeLayout *genesisGetLastNodeLayout()
{
	return wl_state.genesis_node_layout;
}

void genesisWaitForTerrainQueue(TerrainTaskQueue* queue, TerrainEditorSource* source, const char* message)
{
	int count;
	if( message ) 
		loadstart_printf("%s...", message);
	
	// Do all the tasks we've queued up
	do
	{
		TerrainSubtask *subtask = NULL;
		if (eaSize(&queue->edit_tasks) > 0 &&
			eaSize(&queue->edit_tasks[0]->subtasks) > 0)
			subtask = queue->edit_tasks[0]->subtasks[0];
		if (subtask)
			loadstart_printf("%s", terrainQueueGetSubtaskLabel(subtask));
		terrainQueueDoActions(queue, false, source, false);
		if (subtask)
			loadend_printf("Done.");
	} while (!terrainQueueIsEmpty(queue));
	count = terrainCheckReloadBrushImages(source);

	if( message )
		loadend_printf("Done.");
}

void genesisGenerateTerrain(int iPartitionIdx, ZoneMap *zmap, bool saveSource)
{
	if (eaSize(&zmap->layers) == 0)
		return;

	genesisExteriorUpdate(zmap);

	if (wl_state.genesis_node_layout)
	{
		int block;
		int i;
		TerrainEditorSource *source = terrainSourceInitialize();
		TerrainTaskQueue *queue = terrainQueueCreate();
		TerrainEditorSourceLayer *source_layer;
		ZoneMapLayer *layer = zmap->layers[0];

		layerSetMode(layer, LAYER_MODE_GROUPTREE, false, true, false);

		for (block = 0; block < eaSize(&layer->terrain.blocks); block++)
		{
			TerrainBlockRange *range = layer->terrain.blocks[block];
			if (range->interm_file)
			{
				hogFileDestroy(range->interm_file, true);
				range->interm_file = NULL;
			}

			deinitTerrainBlock(range);
		}
		eaDestroyStruct(&layer->terrain.blocks, parse_TerrainBlockRange);

		source_layer = terrainSourceAddLayer(source, layer);
		terrainSourceLoadLayerData(source_layer, false, false, NULL, 0);

		genesisExteriorPaintTerrain(iPartitionIdx, zmap, source_layer, queue, 0, false);

		genesisWaitForTerrainQueue(queue, source, "Performing Tasks");
		//terrainSourceUpdateAllObjects(source_layer, true, false);

		// Copy the relevant materials, objects, etc. to underlying terrain
		layer->terrain.non_playable = !source_layer->playable;
		layer->terrain.exclusion_version = source_layer->exclusion_version;
		layer->terrain.color_shift = source_layer->color_shift;

		eaDestroyEx(&layer->terrain.material_table, StructFreeString);
		eaDestroyStruct(&layer->terrain.object_table, parse_TerrainObjectEntry);

		for (i = 0; i < eaiSize(&source_layer->material_lookup); i++)
			eaPush(&layer->terrain.material_table, StructAllocString(source_layer->source->material_table[source_layer->material_lookup[i]]));

		for (i = 0; i < eaiSize(&source_layer->object_lookup); i++)
			eaPush(&layer->terrain.object_table, StructClone(parse_TerrainObjectEntry, source_layer->source->object_table[source_layer->object_lookup[i]]));

		for (i = 0; i < eaSize(&source_layer->blocks); i++)
			eaPush(&layer->terrain.blocks, StructClone(parse_TerrainBlockRange, source_layer->blocks[i]));

		if( !saveSource ) {
			for (block = 0; block < eaSize(&layer->terrain.blocks); block++)
			{
				TerrainBlockRange *range = layer->terrain.blocks[block];
				StashTableIterator iter;
				StashElement el;

				if (!range->map_cache)
				{
					stashGetIterator(source->heightmaps, &iter);
					while (stashGetNextElement(&iter, &el))
					{
						HeightMap *height_map = stashElementGetPointer(el);
						if (height_map->zone_map_layer == layer)
							eaPush(&range->map_cache, height_map);
					}
				}

				loadstart_printf("Binning terrain layer %d of %d, block %d of %d: (%d, %d) - (%d, %d)...", 
								 1, 1, range->block_idx+1, eaSize(&layer->terrain.blocks),
								 range->range.min_block[0], range->range.min_block[2], range->range.max_block[0], range->range.max_block[2]);

				terrainBinDownsampleAndSave(layer, range);

				loadend_printf(" done.");

				if (layer->bin_status_callback)
				{
					layer->bin_status_callback(layer->bin_status_userdata, block+1, eaSize(&layer->terrain.blocks));
				}
			}
		} else {
			terrainSourceBeginSaveLayer(source_layer);
			terrainQueueSave(queue, source_layer, true, 0);
			genesisWaitForTerrainQueue(queue, source, "Saving Source");
		}

		layerUpdateBounds(layer);
		updateTerrainBlocks(layer);
		layerUpdateGeometry(layer, !saveSource);

		layer->bin_status_userdata = layer->bin_status_callback = NULL;

		terrainSourceDestroy(source);
	}

}

//////////////////////////////////////////////////////////////////////////
// Transmogrify Code
//////////////////////////////////////////////////////////////////////////

static char* genesisFillTags(char** tag_list)
{
	static char* estr = NULL;

	estrDestroy(&estr);

	{
		int it;
		for( it = 0; it != eaSize( &tag_list ); ++it ) {
			estrConcatf( &estr, "%s, ", tag_list[ it ]);
		}
	}

	if( estr ) {
		estrSetSize( &estr, estrLength( &estr ) - 2 );
	}

	return estr;
}

static void genesisFreeString(char *str)
{
	StructFreeString(str);
}

static void genesisTransmogrifyBackdropSounds(GenesisBackdrop *backdrop, GenesisSoundInfo *sound_info, bool one_sound, MersenneTable *table)
{
	//Fall back to sound info when necessary and possible
	if(	sound_info && 
		eaSize(&backdrop->amb_sounds) == 0 && 
		eaSize(&backdrop->amb_hallway_sounds) == 0 )
	{
		if(eaSize(&sound_info->amb_sounds) > 0)
			eaCopyEx(&sound_info->amb_sounds, &backdrop->amb_sounds, strdupFunc, strFreeFunc);
		if(eaSize(&sound_info->amb_hallway_sounds) > 0)
			eaCopyEx(&sound_info->amb_hallway_sounds, &backdrop->amb_hallway_sounds, strdupFunc, strFreeFunc);
	}

	//Pick one of the sounds randomly
	if(one_sound)
	{
		while(eaSize(&backdrop->amb_sounds) > 1)
		{
			int rand_idx = randomMersenneU32(table)%eaSize(&backdrop->amb_sounds);
			char *sound = eaRemoveFast(&backdrop->amb_sounds, rand_idx);
			StructFreeString(sound);
		}
	}
}

static void genesisTransmogrifyBackdropSkies(GenesisBackdrop *backdrop, MersenneTable *table)
{
	int i; 
	if(backdrop->sky_group || !backdrop->rand_sky_group)
		return;
 	backdrop->sky_group = StructCreate(parse_SkyInfoGroup);
	for( i=0; i < eaSize(&backdrop->rand_sky_group->skies) ; i++ )
	{
		GenesisSky *rand_sky = backdrop->rand_sky_group->skies[i];
		if(rand_sky->type == GST_Name)
		{
			SkyInfoOverride *sky_override = StructCreate(parse_SkyInfoOverride);
			SET_HANDLE_FROM_STRING("SkyInfo", rand_sky->str, sky_override->sky);
			if(backdrop->rand_sky_group->override_times)
			{
				sky_override->override_time = true;
				sky_override->time = 24.0f*(randomMersenneF32(table)+1.0f)/2.0f;
			}
			eaPush(&backdrop->sky_group->override_list, sky_override);
		}
		else if(rand_sky->type == GST_Tag)
		{
			SkyInfo *sky = genesisGetRandomItemFromTag(table, "SkyInfo", rand_sky->str, NULL);
			if(sky)
			{
				SkyInfoOverride *sky_override = StructCreate(parse_SkyInfoOverride);
				SET_HANDLE_FROM_REFERENT("SkyInfo", sky, sky_override->sky);
				if(backdrop->rand_sky_group->override_times)
				{
					sky_override->override_time = true;
					sky_override->time = 24.0f*(randomMersenneF32(table)+1.0f)/2.0f;
				}
				eaPush(&backdrop->sky_group->override_list, sky_override);
			}
		}
		else
		{
			genesisRaiseErrorInternal(GENESIS_FATAL_ERROR, "Backdrop", backdrop->name,
									  "Random Sky #%d specifies unknown type", i + 1);
		}
	}
	StructDestroy(parse_GenesisSkyGroup, backdrop->rand_sky_group);
	backdrop->rand_sky_group = NULL;
}

static void genesisTransmogrifyBackdrop(GenesisMapDescBackdrop *backdrop_info, GenesisBackdrop **backdrop_out, MersenneTable *table, char **environment_tags, GenesisSoundInfo *sound_info, bool one_sound, const char *layout_name)
{
	GenesisBackdrop *backdrop = NULL;
	backdrop = GET_REF(backdrop_info->backdrop);
	if(!backdrop && backdrop_info->backdrop_tag_list)
	{
		backdrop = genesisGetRandomItemFromTagList(table, GENESIS_BACKDROP_FILE_DICTIONARY, backdrop_info->backdrop_tag_list, environment_tags);
	}
	if(backdrop)
	{
		(*backdrop_out) = StructClone(parse_GenesisBackdrop, backdrop);
		assert( *backdrop_out );
		genesisTransmogrifyBackdropSounds(*backdrop_out, sound_info, one_sound, table);
		genesisTransmogrifyBackdropSkies(*backdrop_out, table);
	}
	else
	{
		if( backdrop_info->backdrop_tag_list ) {
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(layout_name), "Layout references backdrop by tags \"%s\", but none exist.",
							  genesisFillTags( backdrop_info->backdrop_tag_list ));
		} else {
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(layout_name), "Layout references Backdrop \"%s\", but it does not exist.",
							  REF_STRING_FROM_HANDLE(backdrop_info->backdrop));
		}
	}
}

static ShoeboxPoint* genesisSolarSystemFindPointByName(GenesisSolSysZoneMap *concrete, const char *name)
{
	int j, k;
	GenesisShoebox *shoebox = &concrete->shoebox;
	for( j=0; j < eaSize(&shoebox->point_lists); j++ )
	{
		ShoeboxPointList *point_list = shoebox->point_lists[j];
		for( k=0; k < eaSize(&point_list->points); k++ )
		{
			ShoeboxPoint *point = point_list->points[k];
			if(stricmp(point->name, name)==0)
				return point;
		}
	}
	return NULL;
}

static SSObjSet* genesisSolarSystemFindOrMakeMission(ShoeboxPoint *point, U32 uid, const char *name)
{
	int i;
	SSObjSet *rep;
	for( i=0; i < eaSize(&point->missions); i++ )
	{
		rep = point->missions[i];
		if(rep->mission_uid == uid)
			return rep;
	}
	rep = StructCreate(parse_SSObjSet);
	rep->mission_uid = uid;
	rep->mission_name = StructAllocString(name);
	eaPush(&point->missions, rep);
	return rep;
}

static void genesisSolarSystemInsertIntoPoint(SSObjSet *rep, const char *object_name, GenesisMapDescription *map_desc, int mission_id, char *challenge_name, int *current_id, int count, int spawn_count, GenesisRuntimeErrorContext* source_context, GenesisMissionChallenge *challenge, ShoeboxPoint *point)
{
	GenesisMissionDescription *mission;
	int i;
	char buffer[ 256 ];

	if (mission_id >= 0) {
		mission = map_desc->missions[mission_id];
	} else {
		mission = NULL;
	}
	
	if (!rep->cluster)
	{
		rep->cluster = StructCreate(parse_SSCluster);
		assert(rep->cluster);
		rep->cluster->radius = point->radius;
		rep->cluster->height = 2*point->radius;
		rep->cluster->min_dist = point->min_dist;
		rep->cluster->max_dist = point->max_dist;
	}
	for (i = 0; i < count; i++)
	{
		SSClusterObject *object = StructCreate(parse_SSClusterObject);
		object->lib_obj.obj.name_str = StructAllocString(object_name);
		object->lib_obj.obj.name_uid = objectLibraryUIDFromObjName(object->lib_obj.obj.name_str);
		object->lib_obj.challenge_name = StructAllocString(challenge_name);
		object->lib_obj.challenge_id = (*current_id)++;
		object->lib_obj.source_context = StructClone( parse_GenesisRuntimeErrorContext, source_context );

		if( challenge )
		{
			object->lib_obj.challenge_type = challenge->eType;
			object->lib_obj.patrol_type = SAFE_MEMBER(challenge->pEncounter, eSpacePatrolType);
			if(challenge->pEncounter && challenge->pEncounter->eSpacePatrolType == GENESIS_SPACE_PATROL_Orbit)
			{
				bool outIsShared;
				GenesisMissionChallenge *refChallenge;
				refChallenge = genesisFindChallenge(map_desc, mission, challenge->pEncounter->pcSpacePatRefChallengeName, &outIsShared);

				if (refChallenge)
				{
					if (!outIsShared)
					{
						assert(mission);
						sprintf(buffer, "%s_%s", mission->zoneDesc.pcName, challenge->pEncounter->pcSpacePatRefChallengeName);
					}
					else
						sprintf(buffer, "Shared_%s", challenge->pEncounter->pcSpacePatRefChallengeName);
				}
				else
				{
					genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextChallenge(challenge->pcName, SAFE_MEMBER(mission, zoneDesc.pcName), challenge->pcLayoutName ),
									  "Challenge has patrol challenge \"%s\", but it does not exist.",
									  challenge->pEncounter->pcSpacePatRefChallengeName );
					strcpy(buffer, "");
				}
			}
			else
			{
				if (SAFE_MEMBER(challenge->pEncounter, pcSpacePatRefChallengeName))
					sprintf(buffer, "%s", challenge->pEncounter->pcSpacePatRefChallengeName );
				else
					strcpy(buffer, "");
			}
			object->lib_obj.patrol_ref_name = StructAllocString(buffer);
		}
		object->count = 1;
		object->spawn_count = spawn_count;
		eaPush(&rep->cluster->cluster_objects, object);
	}
}

static void genesisTransmogrifySolarSystemChallenge(GenesisSolSysZoneMap *concrete,
													const char *room_name, const char *object_name, GenesisMissionChallenge *challenge,
													int *current_id, int count, GenesisMapDescription *map_desc, int mission_uid, bool exterior, GenesisEncounterJitter *jitter)
{
	ShoeboxPoint *point = genesisSolarSystemFindPointByName(concrete, room_name);
	char *mission_name;
	GenesisRuntimeErrorContext* source_context;

	// Skip this challenge -- it's in a different layout
	if (stricmp(challenge->pcLayoutName, concrete->layout_name) != 0) {
		return;
	}

	if (mission_uid >= 0 ) {
		mission_name = map_desc->missions[mission_uid]->zoneDesc.pcName;
	} else {
		mission_name = NULL;
	}
	source_context = genesisMakeErrorContextChallenge( challenge->pcName, mission_name, challenge->pcLayoutName );
	
	if(point)
	{
		SSObjSet *rep = genesisSolarSystemFindOrMakeMission(point, mission_uid, mission_name);
		char fullChallengeName[ 256 ];

		if (mission_name)
			sprintf( fullChallengeName, "%s_%s", mission_name, challenge->pcName );
		else
			sprintf( fullChallengeName, "Shared_%s", challenge->pcName );
		genesisSolarSystemInsertIntoPoint(rep, object_name, map_desc, mission_uid, fullChallengeName, current_id, count, challenge->iNumToSpawn, source_context, challenge, point);
	}
	else
	{
		genesisRaiseError( GENESIS_FATAL_ERROR, source_context,
						   "Challenge is trying to place in non-existant room \"%s\".",
						   room_name );
	}

	StructDestroy( parse_GenesisRuntimeErrorContext, source_context );
}

static void genesisTransmogrifySolarSystemPortal(GenesisSolSysZoneMap *concrete,
												 GenesisMissionPortal *portal, GenesisMapDescription *map_desc, int mission_uid)
{
	const char* startLayout = portal->pcStartLayout;
	const char* endLayout;
	
	char *mission_name;
	GenesisRuntimeErrorContext* source_context;

	if( portal->eType == GenesisMissionPortal_BetweenLayouts ) {
		endLayout = portal->pcEndLayout;
	} else {
		endLayout = portal->pcStartLayout;
	}

	if (mission_uid >= 0) {
		mission_name = map_desc->missions[mission_uid]->zoneDesc.pcName;
	} else {
		mission_name = NULL;
	}
	source_context = genesisMakeErrorContextPortal( portal->pcName, mission_name, portal->pcStartLayout );
	
	if(stricmp(startLayout, concrete->layout_name) == 0)
	{
		ShoeboxPoint *point = genesisSolarSystemFindPointByName(concrete, portal->pcStartRoom);
	
		if(point)
		{
			SSObjSet *rep = genesisSolarSystemFindOrMakeMission(point, -1, NULL);
	
			if (!rep->has_portal || portal->eType == GenesisMissionPortal_OneWayOutOfMap)
			{
				SSLibObj *object = StructCreate( parse_SSLibObj );
				char challenge_name_buffer[512];
				object->obj.name_str = StructAllocString( "Goto Spawn Point" );
				object->obj.name_uid = objectLibraryUIDFromObjName(object->obj.name_str);
				object->challenge_name = StructAllocString(genesisMissionPortalSpawnTargetName(challenge_name_buffer, portal, true, mission_name, concrete->layout_name));
				eaPush(&rep->group_refs, object);
				
				if( portal->eType != GenesisMissionPortal_OneWayOutOfMap) {
					rep->has_portal = true;
				}
			}
		}
		else
		{
			genesisRaiseError( GENESIS_FATAL_ERROR, source_context,
							   "Portal is trying to link up non-existant room \"%s\".",
							   portal->pcStartRoom );
		}
	}

	if(stricmp(endLayout, concrete->layout_name) == 0 && portal->eType != GenesisMissionPortal_OneWayOutOfMap)
	{
		ShoeboxPoint *point = genesisSolarSystemFindPointByName(concrete, portal->pcEndRoom);
	
		if(point)
		{
			SSObjSet *rep = genesisSolarSystemFindOrMakeMission(point, -1, NULL);

			if (!rep->has_portal || portal->eType == GenesisMissionPortal_OneWayOutOfMap)
			{
				SSLibObj *object = StructCreate( parse_SSLibObj );
				char challenge_name_buffer[512];
				object->obj.name_str = StructAllocString( "Goto Spawn Point" );
				object->obj.name_uid = objectLibraryUIDFromObjName(object->obj.name_str);
				object->challenge_name = StructAllocString(genesisMissionPortalSpawnTargetName(challenge_name_buffer, portal, false, mission_name, concrete->layout_name));
				eaPush(&rep->group_refs, object);

				if( portal->eType != GenesisMissionPortal_OneWayOutOfMap) {
					rep->has_portal = true;
				}
			}
		}
		else
		{
			genesisRaiseError( GENESIS_FATAL_ERROR, source_context,
							   "Portal is trying to link up non-existant room \"%s\".",
							   portal->pcStartRoom );
		}
	}

	StructDestroy( parse_GenesisRuntimeErrorContext, source_context );
}

static GenesisRoomMission*** genesisRoomFindMissionListByName(GenesisZoneMapRoom **rooms, GenesisZoneMapPath **paths, char *room_name)
{
	int i;
	for( i=0; i < eaSize(&rooms); i++ )
	{
		if(stricmp(rooms[i]->room.name, room_name)==0)
			return &rooms[i]->missions;
	}
	for( i=0; i < eaSize(&paths); i++ )
	{
		if(stricmp(paths[i]->path.name, room_name)==0)
			return &paths[i]->missions;
	}
	return NULL;
}

static GenesisRoomMission* genesisRoomFindOrMakeMission(GenesisRoomMission ***mission_list, int mission_uid, char *mission_name)
{
	int i;
	GenesisRoomMission *mission;
	for( i=0; i < eaSize(mission_list); i++ )
	{
		mission = (*mission_list)[i];
		if (mission->mission_uid == mission_uid)
			return mission;
	}
	mission = StructCreate(parse_GenesisRoomMission);
	mission->mission_name = StructAllocString(mission_name);
	mission->mission_uid = mission_uid;
	eaPush(mission_list, mission);
	return mission;
}

static GenesisRoomMission* genesisRoomFindRoomMissionByName(GenesisZoneMapRoom **rooms, GenesisZoneMapPath **paths, char* room_name, char* mission_name, int mission_uid)
{
	GenesisRoomMission*** room_missions = genesisRoomFindMissionListByName(rooms, paths, room_name);
	if( !room_missions ) {
		return NULL;
	}

	return genesisRoomFindOrMakeMission(room_missions, mission_uid, mission_name);
}


static void genesisPatrolFillPlacement(GenesisPlacementParams* placement, GenesisMissionChallenge* challenge, GenesisMapDescription *map_desc, GenesisMissionDescription *mission_desc)
{
	placement->location = challenge->pEncounter->ePatPlacement;
	
	if( challenge->pEncounter->pcPatRefChallengeName ) {
		bool isShared;
		GenesisMissionChallenge* patRefChallenge = genesisFindChallenge(map_desc, mission_desc, challenge->pEncounter->pcPatRefChallengeName, &isShared);
		if (patRefChallenge) {
			char buffer[ 256 ];
			if (!isShared)
				sprintf(buffer, "%s_%s", mission_desc->zoneDesc.pcName, challenge->pEncounter->pcPatRefChallengeName );
			else
				sprintf(buffer, "Shared_%s", challenge->pEncounter->pcPatRefChallengeName);
			placement->ref_challenge_name = StructAllocString(buffer);
		} else {
			genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextChallenge(challenge->pcName, mission_desc->zoneDesc.pcName, challenge->pcLayoutName),
							  "Challenge has patrol challenge \"%s\", but it does not exist.",
							  challenge->pEncounter->pcPatRefChallengeName );
		}
	}
}

static char** genesisFindConnectedNodes( char* nodeName, GenesisZoneMapRoom** rooms, GenesisZoneMapPath** paths )
{
	char** accum = NULL;
	
	int roomIt;
	int pathIt;
	for( pathIt = 0; pathIt != eaSize( &paths ); ++pathIt ) {
		GenesisZoneMapPath* path = paths[ pathIt ];

		if( stricmp( nodeName, path->path.name ) == 0 ) {
			eaPushEArray( &accum, &path->start_rooms );
			eaPushEArray( &accum, &path->end_rooms );
		}
		
		for( roomIt = 0; roomIt != eaSize( &path->start_rooms ); ++roomIt ) {
			char* roomName = path->start_rooms[ roomIt ];

			if( stricmp( roomName, nodeName ) == 0 ) {
				eaPush( &accum, path->path.name );
			}
		}
		for( roomIt = 0; roomIt != eaSize( &path->end_rooms ); ++roomIt ) {
			char* roomName = path->end_rooms[ roomIt ];

			if( stricmp( roomName, nodeName ) == 0 ) {
				eaPush( &accum, path->path.name );
			}
		}
	}
	
	return accum;
}

static char** genesisFindPathBetweenNodes( char* start_room, char* end_room, GenesisZoneMapRoom** rooms, GenesisZoneMapPath** paths )
{
	StashTable prevNodeTable = stashTableCreateWithStringKeys( eaSize( &rooms ) + eaSize( &paths ), StashDefault );
	char** currentDepthNodes = NULL;
	char** nextDepthNodes = NULL;

	eaPush( &currentDepthNodes, start_room );
	while( eaSize( &currentDepthNodes ) != 0 ) {
		int it;
		for( it = 0; it != eaSize( &currentDepthNodes ); ++it ) {
			char* currentNode = currentDepthNodes[ it ];
			
			char** connectedNodes = genesisFindConnectedNodes( currentDepthNodes[ it ], rooms, paths );
			int connectedIt;
			for( connectedIt = 0; connectedIt != eaSize( &connectedNodes ); ++connectedIt ) {
				char* connectedNode = connectedNodes[ connectedIt ];
				if( !stashFindPointer( prevNodeTable, connectedNode, NULL )) {
					eaPush( &nextDepthNodes, connectedNode );
					stashAddPointer( prevNodeTable, connectedNode, currentNode, false );

					if( stricmp( connectedNode, end_room ) == 0 ) {
						goto found;
					}
				}
			}

			eaDestroy( &connectedNodes );
		}

		eaDestroy( &currentDepthNodes );
		currentDepthNodes = nextDepthNodes;
		nextDepthNodes = NULL;
	}
found:
	eaDestroy( &currentDepthNodes );
	eaDestroy( &nextDepthNodes );

	if( !stashFindPointer( prevNodeTable, end_room, NULL )) {
		return NULL;
	} else {
		char** accum = NULL;
		eaPush( &accum, end_room );
		
		while( stricmp( accum[ 0 ], start_room ) != 0 ) {
			char* prevNode = NULL;
			stashFindPointer( prevNodeTable, accum[ 0 ], &prevNode );
			assert( prevNode );
			eaInsert( &accum, prevNode, 0 );
		}
		
		return accum;
	}
}

static void genesisPatrolInsertIntoRoom( GenesisPatrolObject* patrol, char* room_name, char* mission_name, int mission_uid, GenesisZoneMapRoom **rooms, GenesisZoneMapPath **paths )
{
	GenesisRoomMission* room = genesisRoomFindRoomMissionByName( rooms, paths, room_name, mission_name, mission_uid );
	if( room ) {
		eaPush( &room->patrols, patrol );
	}
}

static void genesisInsertChallengePatrol(GenesisMapDescription *map_desc, GenesisMissionDescription *mission_desc, int mission_uid, char* start_room, GenesisMissionChallenge* challenge, GenesisObject* challenge_object, GenesisZoneMapRoom **rooms, GenesisZoneMapPath **paths, bool exterior)
{
	if( challenge->pEncounter->ePatrolType == GENESIS_PATROL_OtherRoom || challenge->pEncounter->ePatrolType == GENESIS_PATROL_OtherRoom_OneWay ) {
		char** patrolRooms = genesisFindPathBetweenNodes(start_room, challenge->pEncounter->pcPatOtherRoomName, rooms, paths );
		GenesisPatrolType patrolType;

		if( exterior ) {
			genesisRaiseError( GENESIS_ERROR, challenge_object->source_context, "Patrol to other room does not work in exteriors." );
			eaDestroy( &patrolRooms );
			return;
		}

		if( challenge->pEncounter->ePatrolType == GENESIS_PATROL_OtherRoom_OneWay ) {
			patrolType = GENESIS_PATROL_Path_OneWay;
		} else {
			patrolType = GENESIS_PATROL_Path;
		}

		if( eaSize( &patrolRooms ) < 2 ) {
			genesisRaiseError( GENESIS_ERROR, challenge_object->source_context, "Patrol to other room does not span multiple rooms." );
			eaDestroy( &patrolRooms );
			return;
		}

		// first room
		{
			GenesisPatrolObject* firstPatrol = StructCreate( parse_GenesisPatrolObject );
			firstPatrol->owner_challenge = StructClone( parse_GenesisObject, challenge_object );
			firstPatrol->type = patrolType;
			firstPatrol->path_start_is_challenge_pos = true;
			firstPatrol->path_end.location = GenesisChallengePlace_InSpecificDoor;
			firstPatrol->path_end.ref_door_dest_name = StructAllocString( patrolRooms[ 1 ]);

			genesisPatrolInsertIntoRoom( firstPatrol, patrolRooms[ 0 ], SAFE_MEMBER(mission_desc, zoneDesc.pcName), mission_uid, rooms, paths );
		}

		// middle rooms
		{
			int it;
			for( it = 1; it < eaSize( &patrolRooms ) - 1; ++it ) {
				GenesisPatrolObject* middlePatrol = StructCreate( parse_GenesisPatrolObject );
				middlePatrol->owner_challenge = StructClone( parse_GenesisObject, challenge_object );
				middlePatrol->type = patrolType;
				middlePatrol->path_start.location = GenesisChallengePlace_InSpecificDoor;
				middlePatrol->path_start.ref_door_dest_name = StructAllocString( patrolRooms[ it - 1 ]);
				middlePatrol->path_end.location = GenesisChallengePlace_InSpecificDoor;
				middlePatrol->path_end.ref_door_dest_name = StructAllocString( patrolRooms[ it + 1 ]);

				genesisPatrolInsertIntoRoom( middlePatrol, patrolRooms[ it ], SAFE_MEMBER(mission_desc, zoneDesc.pcName), mission_uid, rooms, paths );
			}
		}

		// last room
		{
			GenesisPatrolObject* lastPatrol = StructCreate( parse_GenesisPatrolObject );
			lastPatrol->owner_challenge = StructClone( parse_GenesisObject, challenge_object );
			lastPatrol->type = patrolType;
			lastPatrol->path_start.location = GenesisChallengePlace_InSpecificDoor;
			lastPatrol->path_start.ref_door_dest_name = StructAllocString( patrolRooms[ eaSize( &patrolRooms ) - 2 ]);
			genesisPatrolFillPlacement( &lastPatrol->path_end, challenge, map_desc, mission_desc );
			
			genesisPatrolInsertIntoRoom( lastPatrol, patrolRooms[ eaSize( &patrolRooms ) - 1 ], SAFE_MEMBER(mission_desc, zoneDesc.pcName), mission_uid, rooms, paths );
		}

		eaDestroy( &patrolRooms );
	} else {
		GenesisPatrolObject* patrol = StructCreate( parse_GenesisPatrolObject );
		patrol->owner_challenge = StructClone( parse_GenesisObject, challenge_object );
		patrol->type = challenge->pEncounter->ePatrolType;

		if( patrol->type == GENESIS_PATROL_Path || patrol->type == GENESIS_PATROL_Path_OneWay ) {
			patrol->path_start_is_challenge_pos = true;
			genesisPatrolFillPlacement( &patrol->path_end, challenge, map_desc, mission_desc );
		}
		
		genesisPatrolInsertIntoRoom( patrol, start_room, SAFE_MEMBER(mission_desc, zoneDesc.pcName), mission_uid, rooms, paths );
	}
}

static void genesisInsertIntoRoom(GenesisMapDescription *map_desc, GenesisRoomMission *mission, char* room_name, const char *object_name, char *challenge_name, GenesisMissionChallenge *challenge, int* id, int count, GenesisZoneMapRoom **rooms, GenesisZoneMapPath **paths, GenesisRuntimeErrorContext* source_context, bool exterior, GenesisEncounterJitter *jitter)
{
	GenesisMissionDescription *mission_desc;
	int i;
	char buffer[ 256 ];

	if (mission->mission_uid >= 0) {
		mission_desc = map_desc->missions[mission->mission_uid];
	} else {
		mission_desc = NULL;
	}
	
	for( i=0; i < count; i++ )
	{
		GenesisObject *object = StructCreate(parse_GenesisObject);
		object->obj.name_str = StructAllocString(object_name);
		object->obj.name_uid = objectLibraryUIDFromObjName(object->obj.name_str);
		object->challenge_type = challenge->eType;
		object->challenge_name = StructAllocString(challenge_name);
		object->challenge_id = (*id)++;
		object->challenge_count = challenge->iNumToSpawn;
		object->force_named_object = challenge->bForceNamedObject;
		object->spawn_point_name = StructAllocString(challenge->pcStartSpawnName);
		object->params.facing = challenge->eFacing;
		object->params.location = challenge->ePlacement;
		object->params.rotation_increment = challenge->fRotationIncrement;
		if (challenge->pVolume)
			object->volume = StructClone(parse_GenesisObjectVolume, challenge->pVolume);
		if(eaSize(&challenge->eaChildren))
		{
			eaCopyStructs(&challenge->eaChildren, &object->params.children, parse_GenesisPlacementChildParams);
		}
		if(challenge->fExcludeDist)
		{
			char affinty_group_name[256];
			sprintf(affinty_group_name, "GenesisAutoAffintyGroup_%s", StaticDefineIntRevLookup(GenesisChallengeTypeEnum, challenge->eType));
			object->params.affinity_group = allocAddString(affinty_group_name);
			object->params.exclusion_dist = challenge->fExcludeDist;
		}
		if(jitter)
			StructCopyAll(parse_GenesisEncounterJitter, jitter, &object->params.enc_jitter);		
		if (challenge->pcRefChallengeName) {
			bool isShared;
			GenesisMissionChallenge* refChallenge = genesisFindChallenge(map_desc, mission_desc, challenge->pcRefChallengeName, &isShared);

			if (refChallenge) {
				if (!isShared)
					sprintf(buffer, "%s_%s", mission->mission_name, challenge->pcRefChallengeName);
				else
					sprintf(buffer, "Shared_%s", challenge->pcRefChallengeName);
				object->params.ref_challenge_name = StructAllocString(buffer);
			} else {
				genesisRaiseError(GENESIS_ERROR, source_context,
								  "Challenge has reference challenge \"%s\", but it does not exist.",
								  challenge->pcRefChallengeName );
			}
		}
		object->params.ref_prefab_location = StructAllocString(challenge->pcRefPrefabLocation);
		object->has_patrol = (SAFE_MEMBER( challenge->pEncounter, ePatrolType ) != GENESIS_PATROL_None);
		object->source_context = StructClone( parse_GenesisRuntimeErrorContext, source_context );
		eaPush(&mission->objects, object);

		// Put patrols into the separate data stream
		if( SAFE_MEMBER( challenge->pEncounter, ePatrolType )) {
			genesisInsertChallengePatrol(map_desc, mission_desc, mission->mission_uid, room_name, challenge, object, rooms, paths, exterior);
		}
	}
}

static void genesisTransmogrifyRoomPathChallenge(const char* layout_name, GenesisZoneMapRoom **rooms, GenesisZoneMapPath **paths,
												 char *room_name, const char *object_name, GenesisMissionChallenge *challenge,
												 int *current_id, int count, GenesisMapDescription *map_desc, int mission_uid, bool exterior, GenesisEncounterJitter *jitter)
{
	GenesisRoomMission ***mission_list = genesisRoomFindMissionListByName(rooms, paths, room_name);
	char *mission_name;
	GenesisRuntimeErrorContext* source_context;
	// Skip this challenge -- it's in a different layout
	if (stricmp(challenge->pcLayoutName, layout_name) != 0) {
		return;
	}
	
	if (mission_uid >= 0 ) {
		mission_name = map_desc->missions[mission_uid]->zoneDesc.pcName;
	} else {
		mission_name = NULL;
	}
	source_context = genesisMakeErrorContextChallenge( challenge->pcName, mission_name, challenge->pcLayoutName );
	
	if (mission_list)
	{
		GenesisRoomMission *mission = genesisRoomFindOrMakeMission(mission_list, mission_uid, mission_name);
		char fullChallengeName[ 256 ];

		if (mission_name)
			sprintf( fullChallengeName, "%s_%s", mission_name, challenge->pcName );
		else
			sprintf( fullChallengeName, "Shared_%s", challenge->pcName );
		
		genesisInsertIntoRoom(map_desc, mission, room_name, object_name, fullChallengeName, challenge, current_id, count, rooms, paths, source_context, exterior, jitter);
	}
	else
	{
		genesisRaiseError( GENESIS_FATAL_ERROR, source_context,
						   "Challenge is trying to place in non-existent room \"%s\".",
						   room_name );
	}

	StructDestroy( parse_GenesisRuntimeErrorContext, source_context );
}

static void genesisTransmogrifyRoomPathPortal(const char* layout_name, GenesisZoneMapRoom **rooms, GenesisZoneMapPath **paths,
											  GenesisMissionPortal *portal, GenesisMapDescription *map_desc, int mission_uid)
{
	const char* startLayout = portal->pcStartLayout;
	const char* endLayout;
	
	char* mission_name;
	GenesisRuntimeErrorContext* source_context;

	if( portal->eType == GenesisMissionPortal_BetweenLayouts ) {
		endLayout = portal->pcEndLayout;
	} else {
		endLayout = portal->pcStartLayout;
	}

	if( mission_uid >= 0 ) {
		mission_name = map_desc->missions[mission_uid]->zoneDesc.pcName;
	} else {
		mission_name = NULL;
	}
	source_context = genesisMakeErrorContextPortal( portal->pcName, mission_name, portal->pcStartLayout );
	
	if( stricmp( startLayout, layout_name ) == 0 ) {
		GenesisRoomMission ***mission_list = genesisRoomFindMissionListByName(rooms, paths, portal->pcStartRoom);

		if (mission_list)
		{
			GenesisRoomMission *mission = genesisRoomFindOrMakeMission(mission_list, -1, NULL);
			GenesisZoneMapRoom* room = genesisGetRoomByName( rooms, portal->pcStartRoom );

			if (!portal->pcStartSpawn 
				&& portal->eUseType == GenesisMissionPortal_Volume
				&& (!mission->has_portal || portal->eType == GenesisMissionPortal_OneWayOutOfMap)) {
				GenesisObject *object = StructCreate(parse_GenesisObject);
				char challenge_name_buffer[512];
				object->obj.name_str = StructAllocString( "Goto Spawn Point" );
				object->obj.name_uid = objectLibraryUIDFromObjName(object->obj.name_str);
				object->challenge_name = strdup(genesisMissionPortalSpawnTargetName( challenge_name_buffer, portal, true, mission_name, layout_name ));
				object->params.facing = GenesisChallengeFace_Entrance_Exit;
				object->source_context = StructClone( parse_GenesisRuntimeErrorContext, source_context );
				if( room && room->off_map ) {
					object->params.location = GenesisChallengePlace_ExactCenter;
				}

				eaPush(&mission->objects, object);

				if( portal->eType != GenesisMissionPortal_OneWayOutOfMap) {
					mission->has_portal = true;
				}
			}
		}
		else
		{
			genesisRaiseError( GENESIS_FATAL_ERROR, source_context,
							   "Portal is trying to link up non-existent room \"%s\".",
							   portal->pcStartRoom );
		}
	}

	if( stricmp( endLayout, layout_name ) == 0 && portal->eType != GenesisMissionPortal_OneWayOutOfMap )
	{
		GenesisRoomMission ***mission_list = genesisRoomFindMissionListByName(rooms, paths, portal->pcEndRoom);

		if (mission_list)
		{
			GenesisRoomMission *mission = genesisRoomFindOrMakeMission(mission_list, -1, NULL);
			GenesisZoneMapRoom* room = genesisGetRoomByName( rooms, portal->pcEndRoom );

			if (!portal->pcEndSpawn
				&& portal->eUseType == GenesisMissionPortal_Volume
				&& (!mission->has_portal || portal->eType == GenesisMissionPortal_OneWayOutOfMap)) {
				GenesisObject *object = StructCreate(parse_GenesisObject);
				char challenge_name_buffer[512];
				object->obj.name_str = StructAllocString( "Goto Spawn Point" );
				object->obj.name_uid = objectLibraryUIDFromObjName(object->obj.name_str);
				object->challenge_name = strdup(genesisMissionPortalSpawnTargetName( challenge_name_buffer, portal, false, mission_name, layout_name ));
				object->params.facing = GenesisChallengeFace_Entrance_Exit;
				object->source_context = StructClone( parse_GenesisRuntimeErrorContext, source_context );
				if( room && room->off_map ) {
					object->params.location = GenesisChallengePlace_ExactCenter;
				}

				eaPush(&mission->objects, object);
				
				if( portal->eType != GenesisMissionPortal_OneWayOutOfMap) {
					mission->has_portal = true;
				}
			}
		}
		else
		{
			genesisRaiseError( GENESIS_FATAL_ERROR, source_context,
							   "Portal is trying to link up non-existent room \"%s\".",
							   portal->pcEndRoom );
		}
	}

	StructDestroy( parse_GenesisRuntimeErrorContext, source_context );
}

static void genesisChallengeVerify(GroupDef* def, GenesisMissionChallenge* challenge, GenesisMissionDescription* mission_desc)
{
	GenesisChallengeType effectiveType = GenesisChallenge_None;

	if (def->property_structs.genesis_challenge_properties) {
		effectiveType = def->property_structs.genesis_challenge_properties->type;
	}
	if(   effectiveType == GenesisChallenge_None && challenge->pClickie
		  && IS_HANDLE_ACTIVE(challenge->pClickie->hInteractionDef) ) {
		effectiveType = GenesisChallenge_Clickie;
	}

	if( isProductionEditMode()) {
		// doesn't matter, UGC should be doing validation at a higher level
		return;
		// TOMY TODO, instead of early out, just set the type apropo.
	}

	
	if (challenge->eType == GenesisChallenge_None)
	{
		// doesn't matter what the other type is, always okay
	}
	else if (effectiveType == GenesisChallenge_None)
	{
		genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER(mission_desc, zoneDesc.pcName), challenge->pcLayoutName ),
						  "Challenge object \"%s\" has no challenge properties.", def->name_str);
	}
	else if (effectiveType != challenge->eType)
	{
		genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER(mission_desc, zoneDesc.pcName), challenge->pcLayoutName ),
						  "Challenge type is \"%s\" but challenge object has type \"%s\".",
						  StaticDefineIntRevLookup(GenesisChallengeTypeEnum, challenge->eType),
						  StaticDefineIntRevLookup(GenesisChallengeTypeEnum, def->property_structs.genesis_challenge_properties->type));
	}
}

static void genesisTransmogrifyRoomChallenge(genesisMissionTransChallengeFunc trans_challenge_func, void *user_data, GenesisMapDescription *map_desc, int mission_num, GenesisMissionChallenge *challenge, MersenneTable *table, bool exterior, GenesisEncounterJitter *jitter)
{
	GenesisMissionDescription *mission;
	int k, l;
	int room_cnt = eaSize(&challenge->eaRoomNames);
	GroupDef *object_def = NULL;
	int current_id = 0;
	char* challengeTags = NULL;

	if (mission_num >= 0)
		mission = map_desc->missions[mission_num];
	else
		mission = NULL;

	if(challenge->pcChallengeName)
		object_def = objectLibraryGetGroupDefByName(challenge->pcChallengeName, false);
	else
	{
		if (!challenge->bHeterogenousObjects)
		{
			object_def = genesisGetRandomItemFromTagList1(table, OBJECT_LIBRARY_DICT, challenge->eaChallengeTags, "genesischallenge");
		}
	}

	if (!object_def && !challenge->bHeterogenousObjects)
	{
		genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER(mission, zoneDesc.pcName), challenge->pcLayoutName ),
						  "Could not find object for challenge." );

		estrDestroy( &challengeTags );
		return;
	}
	assert(object_def || challenge->bHeterogenousObjects);

	if (challenge->eType != GenesisChallenge_None && !challenge->bHeterogenousObjects)
	{
		genesisChallengeVerify(object_def, challenge, mission);
	}
	for( k=0; k < room_cnt; k++ )
	{
		char *room_name = NULL;

		//In the case we only have one object, pick a random room
		if(challenge->iCount==1)
			room_name = challenge->eaRoomNames[randomMersenneU32(table)%room_cnt];
		else
			room_name = challenge->eaRoomNames[k];
					
		if (!object_def && challenge->bHeterogenousObjects) {
			for( l=0; l < (challenge->iCount + k)/room_cnt; l++ )
			{
				GroupDef *vary_object_def = genesisGetRandomItemFromTagList1(table, OBJECT_LIBRARY_DICT, challenge->eaChallengeTags, "genesischallenge");
				if (!vary_object_def)
				{
					genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextChallenge( challenge->pcName, SAFE_MEMBER(mission, zoneDesc.pcName), challenge->pcLayoutName ),
									  "Could not find object for challenge.");					
					break;
				}
				genesisChallengeVerify(vary_object_def, challenge, mission);
					
				trans_challenge_func(user_data, room_name, vary_object_def->name_str, challenge, &current_id, 1, map_desc, mission_num, exterior, jitter);
			}
		} else {
			trans_challenge_func(user_data, room_name, object_def->name_str, challenge, &current_id, (challenge->iCount + k)/room_cnt, map_desc, mission_num, exterior, jitter);
		}
	}
}

static void genesisTransmogrifyRoomChallengesAndPortals(
		genesisMissionTransChallengeFunc trans_challenge_func, genesisMissionTransPortalFunc trans_portal_func,
		void *user_data, GenesisMapDescription *map_desc, MersenneTable *table, bool exterior, GenesisEncounterJitter *jitter)
{
	int i, j;
	for (i = 0; i < eaSize(&map_desc->missions); i++)
	{
		GenesisMissionDescription *mission = map_desc->missions[i];
		for (j = 0; j < eaSize(&mission->eaChallenges); j++)
		{
			genesisTransmogrifyRoomChallenge(trans_challenge_func, user_data, map_desc, i, mission->eaChallenges[j], table, exterior, jitter);
		}
		for (j = 0; j < eaSize(&mission->zoneDesc.eaPortals); j++)
		{
			trans_portal_func(user_data, mission->zoneDesc.eaPortals[j], map_desc, i);
		}
	}
	for (i = 0; i < eaSize(&map_desc->shared_challenges); i++)
	{
		genesisTransmogrifyRoomChallenge(trans_challenge_func, user_data, map_desc, -1, map_desc->shared_challenges[i], table, exterior, jitter);
	}
}

static void genesisAssociateSideTrails(GenesisMissionChallenge *challenge, int rooms_made)
{
	int i;
	bool uses_side_trail = false;
	//Only consider challenges that need these rooms
	for ( i=0; i < eaSize(&challenge->eaRoomNames); i++ )
	{
		char *room_name = challenge->eaRoomNames[i];
		if(utils_stricmp(room_name, GENESIS_SIDE_TRAIL_NAME)==0)
		{
			eaRemove(&challenge->eaRoomNames, i);
			StructFreeString(room_name);
			uses_side_trail = true;
			break;
		}
	}
	if(uses_side_trail)
	{
		char buf[256];
		for ( i=0; i < rooms_made; i++ )
		{
			sprintf(buf, "AutoSideTrailRoom_%02d", i+1);
			eaPush(&challenge->eaRoomNames, StructAllocString(buf));
		}
	}
}

static void genesisCheckMaxSideTrailNeeded(GenesisMissionChallenge *challenge, int *max_num_rooms_needed)
{
	int i;
	bool uses_side_trail = false;
	//Only consider challenges that need these rooms
	for ( i=0; i < eaSize(&challenge->eaRoomNames); i++ )
	{
		char *room_name = challenge->eaRoomNames[i];
		if(utils_stricmp(room_name, GENESIS_SIDE_TRAIL_NAME)==0)
		{
			uses_side_trail = true;
			break;
		}
	}
	if(uses_side_trail)
	{
		//Number of rooms needed is based on how many challenges we are placing and how many other rooms this challenge is being placed in.
		int challenge_count = MAX(challenge->iCount, 1);
		int num_rooms_needed = challenge_count / eaSize(&challenge->eaRoomNames);
		num_rooms_needed = MAX(num_rooms_needed, 1);
		*max_num_rooms_needed = MAX(num_rooms_needed, *max_num_rooms_needed);
	}
}

static void genesisExpandSideTrailChallenges(GenesisZoneMapRoom ***rooms, GenesisZoneMapPath ***paths, GenesisMapDescription *map_desc, MersenneTable *table, const char *layout_name)
{
	int i, j, k;
	int max_num_rooms_needed=0;
	GenesisZoneMapRoom **sorted_rooms=NULL;
	//Only effects maps with missions and at least one room
	if(eaSize(&map_desc->missions) == 0 && eaSize(&map_desc->shared_challenges) == 0)
		return;
	if(eaSize(rooms) == 0)
	{
		genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(layout_name), "Side trails can not be placed without any rooms.");
		return;
	}
	//Find out how many rooms we need
	for (i = 0; i < eaSize(&map_desc->missions); i++)
	{
		GenesisMissionDescription *mission = map_desc->missions[i];
		for (j = 0; j < eaSize(&mission->eaChallenges); j++)
			genesisCheckMaxSideTrailNeeded(mission->eaChallenges[j], &max_num_rooms_needed);
	}
	for (i = 0; i < eaSize(&map_desc->shared_challenges); i++)
		genesisCheckMaxSideTrailNeeded(map_desc->shared_challenges[i], &max_num_rooms_needed);
	//If no rooms need then nothing to do
	if(max_num_rooms_needed == 0)
		return;
	//Sort the existing rooms based on how ok it is to add an additional path
	for ( i=0; i < eaSize(rooms); i++ )
	{
		GenesisZoneMapRoom *room = (*rooms)[i];
		//Find the number of paths connected to this room
		room->path_count = 0;
		for ( j=0; j < eaSize(paths); j++ )
		{
			GenesisZoneMapPath *path = (*paths)[j];
			for ( k=0; k < eaSize(&path->start_rooms); k++ )
			{
				if(utils_stricmp(path->start_rooms[k], room->room.name) == 0)
					room->path_count++;
			}
			for ( k=0; k < eaSize(&path->end_rooms); k++ )
			{
				if(utils_stricmp(path->end_rooms[k], room->room.name) == 0)
					room->path_count++;
			}
		}
		//Determine if start or exit room
		room->is_start_end = false;
		if(	utils_stricmp(map_desc->missions[0]->zoneDesc.startDescription.pcStartRoom, room->room.name)==0 ||
			utils_stricmp(map_desc->missions[0]->zoneDesc.startDescription.pcExitRoom, room->room.name)==0 )
			room->is_start_end = true;
		for ( j=0; j < eaSize(&sorted_rooms); j++ )
		{
			//Rooms with less than or equal to 3 paths,
			//then rooms that are start or end,
			//then rooms that have more than 3 paths
			GenesisZoneMapRoom *c_room = sorted_rooms[j];
			int val=0, c_val=0;
			if(room->path_count <= 3)
				val = (room->is_start_end ? 1 : 0);
			else 
				val = (room->is_start_end ? 2 : 3);
			if(c_room->path_count <= 3)
				c_val = (c_room->is_start_end ? 1 : 0);
			else 
				c_val = (c_room->is_start_end ? 2 : 3);
			if(val < c_val)
				break;
			if(val > c_val)
				continue;
			if(room->path_count <= c_room->path_count)
				break;
		}
		eaInsert(&sorted_rooms, room, j);
	}
	//Make the new rooms and paths
	for ( i=0; i < max_num_rooms_needed; i++ )
	{
		char buf[256];
		GenesisZoneMapRoom *room = StructCreate(parse_GenesisZoneMapRoom);
		GenesisZoneMapPath *path = StructCreate(parse_GenesisZoneMapPath);
		sprintf(buf, "AutoSideTrailRoom_%02d", i+1);
		room->room.name = StructAllocString(buf);
		room->room.depth = room->room.width = GENESIS_EXTERIOR_KIT_SIZE*5;
		room->detail_seed = randomMersenneU32(table);
		room->side_trail = true;
		sprintf(buf, "AutoSideTrailPath_%02d", i+1);
		path->path.name = StructAllocString(buf);
		path->path.min_length = GENESIS_EXTERIOR_KIT_SIZE * (map_desc->exterior_layout->min_side_trail_length ? map_desc->exterior_layout->min_side_trail_length : 4);
		path->path.max_length = GENESIS_EXTERIOR_KIT_SIZE * (map_desc->exterior_layout->max_side_trail_length ? map_desc->exterior_layout->max_side_trail_length : 12);
		path->path.width = GENESIS_EXTERIOR_KIT_SIZE/2.0f;
		path->detail_seed = randomMersenneU32(table);
		path->side_trail = true;
		eaPush(&path->start_rooms, StructAllocString(sorted_rooms[i%eaSize(&sorted_rooms)]->room.name));
		eaPush(&path->end_rooms, StructAllocString(room->room.name));
		eaPush(rooms, room);
		eaPush(paths, path);
	}
	eaDestroy(&sorted_rooms);
	//Remove the dummy room and add the correct rooms to the challenges
	//Find out how many rooms we need
	for (i = 0; i < eaSize(&map_desc->missions); i++)
	{
		GenesisMissionDescription *mission = map_desc->missions[i];
		for (j = 0; j < eaSize(&mission->eaChallenges); j++)
			genesisAssociateSideTrails(mission->eaChallenges[j], max_num_rooms_needed);
	}
	for (i = 0; i < eaSize(&map_desc->shared_challenges); i++)
		genesisAssociateSideTrails(map_desc->shared_challenges[i], max_num_rooms_needed);
}

void genesisTransmogrifyORTags(SSLibObj ***group_refs, SSTagObj ***tag_list, MersenneTable *table, bool destroy_tags, char **environment_tags)
{
	int j;
	for( j=0; j < eaSize(tag_list); j++ )
	{
		SSTagObj *obj_tags = (*tag_list)[j];
		char **tags = obj_tags->tags;
		GroupDef *object_def = genesisGetRandomItemFromTagList(table, OBJECT_LIBRARY_DICT, tags, environment_tags);
		if(object_def)
		{
			SSLibObj *obj = StructCreate(parse_SSLibObj);
			obj->obj.name_str = StructAllocString(object_def->name_str);
			obj->obj.name_uid = object_def->name_uid;
			StructCopy(parse_SSOffset, &obj_tags->offset, &obj->offset, 0, 0, 0);
			eaPush(group_refs, obj);
		}
		if(destroy_tags)
			StructDestroy(parse_SSTagObj, obj_tags);
	}
	if(destroy_tags)
		eaDestroy(tag_list);
}

void genesisTransmogrifySolarSystemPointLists(GenesisShoebox *concrete_shoebox, MersenneTable *table, char **environment_tags)
{
	int i, j;
	for( i=0; i < eaSize(&concrete_shoebox->point_lists); i++ )
	{
		ShoeboxPointList *point_list = concrete_shoebox->point_lists[i];
		if(point_list->orbit_object)
			genesisTransmogrifyORTags(&point_list->orbit_object->group_refs, &point_list->orbit_object->object_tags, table, true, environment_tags);
		genesisTransmogrifyORTags(&point_list->curve_objects, &point_list->curve_objects_tags, table, true, environment_tags);

		for( j=0; j < eaSize(&point_list->points); j++ )
		{
			ShoeboxPoint *point = point_list->points[j];
			if(point->radius > 0)
			{
				if(!point->point_rep)
					point->point_rep = StructCreate(parse_SSObjSet);
				if(!point->point_rep->cluster)
				{
					point->point_rep->cluster = StructCreate(parse_SSCluster);
					point->point_rep->cluster->radius = point->radius;
					point->point_rep->cluster->height = 2*point->radius;
					point->point_rep->cluster->min_dist = point->min_dist;
					point->point_rep->cluster->max_dist = point->max_dist;
				}
			}
		}
	}
}

void genesisTransmogrifyExpandDetailObjects(SSLibObj ***objects, MersenneTable *table)
{
	int i, j;
	for ( i=eaSize(objects)-1; i >= 0 ; i-- )
	{
		SSLibObj *object = (*objects)[i];
		//If max count is set
		if(object->offset.max_count > 0)
		{
			int min_cnt = MAX(0, object->offset.min_count);
			int max_cnt = MAX(min_cnt, object->offset.max_count);
			int diff = max_cnt-min_cnt;
			U32 rand = randomMersenneU32(table);
			int count = rand%(diff+1) + min_cnt;
			if(count < 1)
			{
				eaRemove(objects, i);
				StructDestroy(parse_SSLibObj, object);
			}
			else
			{
				count--;//Because there already is one in the list
				for ( j=0; j < count; j++ )
				{
					SSLibObj *new_object = StructCreate(parse_SSLibObj);
					StructCopyAll(parse_SSLibObj, object, new_object);
					eaPush(objects, new_object);
				}
			}
		}
	}
}

void genesisTransmogrifySolarSystem(U32 seed, U32 detail_seed, GenesisMapDescription *map_desc, GenesisSolSysLayout *vague, GenesisSolSysZoneMap *concrete)
{
	int i;
	char **environment_tags = vague->environment_tags;
	MersenneTable *table = mersenneTableCreate(seed);

	concrete->layout_seed = vague->common_data.layout_seed;

	genesisTransmogrifyBackdrop(&vague->common_data.backdrop_info, &concrete->backdrop, table, environment_tags, NULL, true, vague->name);

	StructCopyString( &concrete->layout_name, vague->name );
	concrete->no_sharing_detail = vague->common_data.no_sharing_detail;
	if( concrete->no_sharing_detail && eaSize( &map_desc->shared_challenges ) > 0 ) {
		genesisRaiseError( GENESIS_WARNING, genesisMakeTempErrorContextLayout(vague->name), "NoSharingDetail is true, but there are SharedChallenges.  The SharedChallenges will not be in the same position across different missions." );
	}

	{
		GenesisShoeboxLayout *vague_shoebox = &vague->shoebox;
		GenesisShoebox *concrete_shoebox = &concrete->shoebox;

		eaCopyStructs(&vague_shoebox->detail_objects, &concrete_shoebox->detail_objects, parse_SSLibObj);
		genesisTransmogrifyORTags(&concrete_shoebox->detail_objects, &vague_shoebox->detail_objects_tags, table, false, environment_tags);
		genesisTransmogrifyExpandDetailObjects(&concrete_shoebox->detail_objects, table);	

		eaCopyStructs(&vague_shoebox->point_lists, &concrete_shoebox->point_lists, parse_ShoeboxPointList);
		genesisTransmogrifySolarSystemPointLists(concrete_shoebox, table, environment_tags);
	}

	for (i = 0; i < eaSize(&map_desc->missions); i++)
	{
		// Only one start spawn point can be made, use the one for the first mission
		if( i == 0 && stricmp( vague->name, map_desc->missions[i]->zoneDesc.startDescription.pcStartLayout ) == 0 )
		{
			ShoeboxPoint *point = genesisSolarSystemFindPointByName(concrete, map_desc->missions[i]->zoneDesc.startDescription.pcStartRoom);
			if (point)
			{
				SSObjSet *rep = genesisSolarSystemFindOrMakeMission(point, i, map_desc->missions[i]->zoneDesc.pcName);
				SSLibObj *new_object = StructCreate(parse_SSLibObj);
				if (IS_HANDLE_ACTIVE(map_desc->missions[i]->zoneDesc.startDescription.hStartTransitionOverride))
				{
					COPY_HANDLE(new_object->start_spawn_using_transition, map_desc->missions[i]->zoneDesc.startDescription.hStartTransitionOverride);
				}
				else
				{
					new_object->obj.name_str = StructAllocString("Start Spawn Point");
				}
				new_object->challenge_name = StructAllocString("Start");
				new_object->source_context = genesisMakeErrorAutoGen(genesisMakeErrorContextDictionary( OBJECT_LIBRARY_DICT, new_object->obj.name_str ) );
				eaPush(&rep->group_refs, new_object);
			}
		}
	}

	genesisTransmogrifyRoomChallengesAndPortals(genesisTransmogrifySolarSystemChallenge, genesisTransmogrifySolarSystemPortal,
												concrete, map_desc, table, false, NULL);
	mersenneTableFree(table);

	concrete->tmog_version = TMOG_SOLAR_SYSTEM_VERSION;
}

static void genesisTransmogrifyRoomDetailKit(GenesisDetailKitAndDensity *concrete_room_kit, GenesisRoomDetailKitLayout *vague_room_kit, GenesisDetailKitLayout *layout_kit, MersenneTable *table)
{
	//First check if kit is defined on the room
	GenesisDetailKit *kit = GET_REF(vague_room_kit->detail_kit);
	//Then check if tagged on the room
	if(!kit)	
		kit = genesisGetRandomItemFromTagList(table, GENESIS_DETAIL_DICTIONARY, vague_room_kit->detail_tag_list, NULL);
	//Then check if defined on the layout
	if(!kit)
		kit = GET_REF(layout_kit->detail_kit);
	//Then check if tagged on the layout
	if(!kit)
	{
		if(layout_kit->vary_per_room)
			kit = genesisGetRandomItemFromTagList(table, GENESIS_DETAIL_DICTIONARY, layout_kit->detail_tag_list, NULL);
		else if (layout_kit->random_detail_kit)
			kit = layout_kit->random_detail_kit;
		else
			kit = layout_kit->random_detail_kit = genesisGetRandomItemFromTagList(table, GENESIS_DETAIL_DICTIONARY, layout_kit->detail_tag_list, NULL);
	}
	//If we found a kit then apply it
	if(kit)
		SET_HANDLE_FROM_REFERENT(GENESIS_DETAIL_DICTIONARY, kit, concrete_room_kit->details);

	concrete_room_kit->detail_density = layout_kit->detail_density;
	if(vague_room_kit->detail_density_override)
		concrete_room_kit->detail_density = vague_room_kit->detail_density;
}

static void genesisTransmogrifyRooms(GenesisLayoutRoom **vague_rooms, GenesisZoneMapRoom ***concrete_rooms, MersenneTable *table, MersenneTable *detail_table, GenesisDetailKitLayout *vague_layout_kits[2], const char *layout_name)
{
	int i;
	GenesisLayoutRoom *start_room = NULL;
	for( i=0; i < eaSize(&vague_rooms); i++ )
	{
		GenesisLayoutRoom *vague_room = vague_rooms[i];
		GenesisZoneMapRoom *concrete_room = StructCreate(parse_GenesisZoneMapRoom);
		GenesisRoomDef *room_ref = NULL;

		if (!vague_room->name)
		{
			char buffer[32];
			sprintf( buffer, "#%d", i+1);
			genesisRaiseError(GENESIS_FATAL_ERROR, 
				genesisMakeTempErrorContextRoom(buffer, layout_name), 
				"Room has no name.");
			StructDestroy(parse_GenesisZoneMapRoom, concrete_room);
			return;
		}

		{
			U32 detail_seed = randomMersenneU32(detail_table);//If we don't take the seed, then giving a seed to one room will reseed others. 
			concrete_room->detail_seed = vague_room->detail_seed ? vague_room->detail_seed : detail_seed;
		}

		room_ref = GET_REF(vague_room->room);
		if (!room_ref)
		{
			room_ref = genesisGetRandomItemFromTagList(table, GENESIS_ROOM_DEF_DICTIONARY, vague_room->room_tag_list, NULL);
		}
		if (!room_ref)
		{
			if( IS_HANDLE_ACTIVE(vague_room->room)) {
				genesisRaiseError(GENESIS_FATAL_ERROR, 
					genesisMakeTempErrorContextRoom(vague_room->name, layout_name), 
					"Could not find RoomDef \"%s\".", REF_STRING_FROM_HANDLE(vague_room->room));
			} else {
				genesisRaiseError(GENESIS_FATAL_ERROR, 
					genesisMakeTempErrorContextRoom(vague_room->name, layout_name), 
					"Could not find RoomDef from tags \"%s\".", genesisFillTags( vague_room->room_tag_list ));
			}
			
			StructDestroy(parse_GenesisZoneMapRoom, concrete_room);
			return;
		}
		StructCopyAll(parse_GenesisRoomDef, room_ref, &concrete_room->room);
		if(concrete_room->room.name)
			StructFreeString(concrete_room->room.name);
		concrete_room->room.name = StructAllocString(vague_room->name);
		concrete_room->off_map = vague_room->off_map;

		genesisTransmogrifyRoomDetailKit(&concrete_room->detail_kit_1, &vague_room->detail_kit_1, vague_layout_kits[GDKT_Detail_1], table);
		genesisTransmogrifyRoomDetailKit(&concrete_room->detail_kit_2, &vague_room->detail_kit_2, vague_layout_kits[GDKT_Detail_2], table);

		eaPush(concrete_rooms, concrete_room);
	}
}

static void genesisTransmogrifyPaths(GenesisLayoutPath **vague_paths, GenesisZoneMapPath ***concrete_paths, MersenneTable *table, MersenneTable *detail_table, GenesisDetailKitLayout *vague_layout_kits[2], const char *layout_name)
{
	int i, j;
	for( i=0; i < eaSize(&vague_paths); i++ )
	{
		GenesisLayoutPath *vague_path = vague_paths[i];
		GenesisZoneMapPath *concrete_path = StructCreate(parse_GenesisZoneMapPath);
		GenesisPathDef *path_ref = NULL;

		if (!vague_path->name)
		{
			char buffer[32];
			sprintf(buffer, "#%d", i+1);
			genesisRaiseError(GENESIS_FATAL_ERROR, 
				genesisMakeTempErrorContextPath(buffer, layout_name), 
				"Path has no name.");
			StructDestroy(parse_GenesisZoneMapPath, concrete_path);
			return;
		}

		{
			U32 detail_seed = randomMersenneU32(detail_table);//If we don't take the seed, then giving a path to one room will reseed others. 
			concrete_path->detail_seed = vague_path->detail_seed ? vague_path->detail_seed : detail_seed;
		}

		path_ref = GET_REF(vague_path->path);
		if (!path_ref)
		{
			path_ref = genesisGetRandomItemFromTagList(table, GENESIS_PATH_DEF_DICTIONARY, vague_path->path_tag_list, NULL);
		}
		if (!path_ref)
		{
			if (IS_HANDLE_ACTIVE(vague_path->path)) {
				genesisRaiseError(GENESIS_FATAL_ERROR, 
					genesisMakeTempErrorContextPath(vague_path->name, layout_name), 
					"Could not find PathDef \"%s\".", REF_STRING_FROM_HANDLE(vague_path->path));
			} else {
				genesisRaiseError(GENESIS_FATAL_ERROR, 
					genesisMakeTempErrorContextPath(vague_path->name, layout_name), 
					"Could not find PathDef by tags \"%s\".", genesisFillTags(vague_path->path_tag_list));
			}
			StructDestroy(parse_GenesisZoneMapPath, concrete_path);
			return;
		}
		StructCopyAll(parse_GenesisPathDef, path_ref, &concrete_path->path);
		if(vague_path->min_length)
			concrete_path->path.min_length = vague_path->min_length;
		if(vague_path->max_length)
			concrete_path->path.max_length = vague_path->max_length;
		if(concrete_path->path.name)
			StructFreeString(concrete_path->path.name);
		concrete_path->path.name = StructAllocString(vague_path->name);

		for ( j=0; j < eaSize(&vague_path->start_rooms); j++ )
			eaPush(&concrete_path->start_rooms, StructAllocString(vague_path->start_rooms[j]));
		for ( j=0; j < eaSize(&vague_path->end_rooms); j++ )
			eaPush(&concrete_path->end_rooms, StructAllocString(vague_path->end_rooms[j]));

		genesisTransmogrifyRoomDetailKit(&concrete_path->detail_kit_1, &vague_path->detail_kit_1, vague_layout_kits[GDKT_Detail_1], table);
		genesisTransmogrifyRoomDetailKit(&concrete_path->detail_kit_2, &vague_path->detail_kit_2, vague_layout_kits[GDKT_Detail_2], table);

		eaPush(concrete_paths, concrete_path);
	}
}

GenesisZoneMapRoom *genesisGetRoomByName(GenesisZoneMapRoom **rooms, const char *name)
{
	int i;
	for (i = 0; i < eaSize(&rooms); i++)
	{
		if (stricmp(rooms[i]->room.name, name) == 0)
			return rooms[i];
	}
	return NULL;
}

static void genesisTransmogrifyVerifyLayout(GenesisZoneMapRoom **rooms, GenesisZoneMapPath **paths, GenesisMissionPortal **teleports, bool specified, const char *layout_name)
{
	int i, j;
	int stack_size = 0;
	GenesisZoneMapRoom **room_stack = NULL;
	if (eaSize(&rooms) == 0)
	{
		genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(layout_name), "Map has no rooms.");
		return;
	}

	if (specified)
	{
		// Test overlap
		for (i = 0; i < eaSize(&rooms); i++)
		{
			GenesisZoneMapRoom *room1 = rooms[i];
			for (j = i+1; j < eaSize(&rooms); j++)
			{
				GenesisZoneMapRoom *room2 = rooms[j];
				if (room1->iPosX <= room2->iPosX+room2->room.width-1 &&
					room2->iPosX <= room1->iPosX+room1->room.width-1 &&
					room1->iPosZ <= room2->iPosZ+room2->room.depth-1 &&
					room2->iPosZ <= room1->iPosZ+room1->room.depth-1)
				{
					genesisRaiseError(GENESIS_FATAL_ERROR, 
						genesisMakeTempErrorContextRoom(room1->room.name, layout_name), 
						"Room overlaps with room %s.", room2->pcVisibleName ? room2->pcVisibleName : room2->room.name);
					genesisRaiseError(GENESIS_FATAL_ERROR, 
						genesisMakeTempErrorContextRoom(room2->room.name, layout_name), 
						"Room overlaps with room %s.", room1->pcVisibleName ? room1->pcVisibleName : room1->room.name);
				}
			}
			for (j = 0; j < eaSize(&paths); j++)
			{
				int k;
				GenesisZoneMapPath *path = paths[j];
				int *hallway_points = NULL;
				if (eaiSize(&path->control_points) > 0)
				{
					genesisGenerateHallwayFromControlPoints(path->control_points, &hallway_points);
					for (k = 0; k < eaiSize(&hallway_points); k += 4)
					{
						if (hallway_points[k] >= (S32)room1->iPosX && hallway_points[k] < (S32)room1->iPosX+room1->room.width &&
							hallway_points[k+2] >= (S32)room1->iPosZ && hallway_points[k+2] < (S32)room1->iPosZ+room1->room.depth)
						{
							genesisRaiseError(GENESIS_FATAL_ERROR, 
								genesisMakeTempErrorContextRoom(room1->room.name, layout_name), 
								"Room overlaps with path %s.", path->pcVisibleName ? path->pcVisibleName : path->path.name);
							genesisRaiseError(GENESIS_FATAL_ERROR, 
								genesisMakeTempErrorContextPath(path->path.name, layout_name), 
								"Path overlaps with room %s.", room1->pcVisibleName ? room1->pcVisibleName : room1->room.name);
							break;
						}
					}
					eaiDestroy(&hallway_points);
				}
			}
		}
		for (i = 0; i < eaSize(&paths); i++)
		{
			int k, l;
			GenesisZoneMapPath *path1 = paths[i];
			int *hallway_points_1 = NULL;
			bool found = false;
			genesisGenerateHallwayFromControlPoints(path1->control_points, &hallway_points_1);
			if (eaiSize(&hallway_points_1) > 0)
			{
				if (eaiSize(&hallway_points_1) < GENESIS_MIN_PATH_LENGTH*4)
				{
					genesisRaiseError(GENESIS_FATAL_ERROR, 
						genesisMakeTempErrorContextPath(path1->path.name, layout_name), 
						"Path shorter than minimum length of %d", GENESIS_MIN_PATH_LENGTH);
				}
				for (k = 0; k < eaiSize(&hallway_points_1); k += 4)
				{
					for (l = k+4; l < eaiSize(&hallway_points_1); l += 4)
						if (hallway_points_1[k] == hallway_points_1[l] &&
							hallway_points_1[k+2] == hallway_points_1[l+2])
						{
							genesisRaiseError(GENESIS_FATAL_ERROR, 
								genesisMakeTempErrorContextPath(path1->path.name, layout_name), 
								"Path overlaps with itself.");
							found = true;
							break;
						}
						if (found)
							break;
				}
				for (j = i+1; j < eaSize(&paths); j++)
				{
					GenesisZoneMapPath *path2 = paths[j];
					int *hallway_points_2 = NULL;
					found = false;
					genesisGenerateHallwayFromControlPoints(path2->control_points, &hallway_points_2);
					for (k = 0; k < eaiSize(&hallway_points_1); k += 4)
					{
						for (l = 0; l < eaiSize(&hallway_points_2); l += 4)
							if (hallway_points_1[k] == hallway_points_2[l] &&
								hallway_points_1[k+2] == hallway_points_2[l+2])
							{
								genesisRaiseError(GENESIS_FATAL_ERROR, 
									genesisMakeTempErrorContextPath(path1->path.name, layout_name), 
									"Path overlaps with path %s.", path2->pcVisibleName ? path2->pcVisibleName : path2->path.name);
								genesisRaiseError(GENESIS_FATAL_ERROR, 
									genesisMakeTempErrorContextPath(path2->path.name, layout_name), 
									"Path overlaps with path %s.", path1->pcVisibleName ? path1->pcVisibleName : path1->path.name);
								found = true;
								break;
							}
							if (found)
								break;
					}
					eaiDestroy(&hallway_points_2);
				}
			}
			eaiDestroy(&hallway_points_1);
		}
	}
	// Test connectivity
	eaPush(&room_stack, rooms[0]);
	do {
		stack_size = eaSize(&room_stack);
		for (i = 0; i < eaSize(&paths); i++)
		{
			for (j = 0; j < eaSize(&paths[i]->start_rooms); j++)
			{
				GenesisZoneMapRoom *room = genesisGetRoomByName(rooms, paths[i]->start_rooms[j]);
				if (room && eaFind(&room_stack, room) != -1)
				{
					int k;
					for (k = 0; k < eaSize(&paths[i]->start_rooms); k++)
					{
						room = genesisGetRoomByName(rooms, paths[i]->start_rooms[k]);
						if (eaFind(&room_stack, room) == -1)
							eaPush(&room_stack, room);
					}
					for (k = 0; k < eaSize(&paths[i]->end_rooms); k++)
					{
						room = genesisGetRoomByName(rooms, paths[i]->end_rooms[k]);
						if (eaFind(&room_stack, room) == -1)
							eaPush(&room_stack, room);
					}
				}
			}
			for (j = 0; j < eaSize(&paths[i]->end_rooms); j++)
			{
				GenesisZoneMapRoom *room = genesisGetRoomByName(rooms, paths[i]->end_rooms[j]);
				if (room && eaFind(&room_stack, room) != -1)
				{
					int k;
					for (k = 0; k < eaSize(&paths[i]->start_rooms); k++)
					{
						room = genesisGetRoomByName(rooms, paths[i]->start_rooms[k]);
						if (eaFind(&room_stack, room) == -1)
							eaPush(&room_stack, room);
					}
					for (k = 0; k < eaSize(&paths[i]->end_rooms); k++)
					{
						room = genesisGetRoomByName(rooms, paths[i]->end_rooms[k]);
						if (eaFind(&room_stack, room) == -1)
							eaPush(&room_stack, room);
					}
				}
			}
		}
		if (teleports)
			for (i = 0; i < eaSize(&teleports); i++)
			{
				GenesisZoneMapRoom *room1 = genesisGetRoomByName(rooms, teleports[i]->pcStartRoom);
				GenesisZoneMapRoom *room2 = genesisGetRoomByName(rooms, teleports[i]->pcEndRoom);
				if (room1 && eaFind(&room_stack, room1) != -1)
				{
					if (room2 && eaFind(&room_stack, room2) == -1)
						eaPush(&room_stack, room2);
				}
				else
				{
					if (room1 && room2 && eaFind(&room_stack, room2) != -1)
						eaPush(&room_stack, room1);
				}
			}
	} while (eaSize(&room_stack) != stack_size);
	if (eaSize(&room_stack) != eaSize(&rooms))
	{
		for (i = 0; i < eaSize(&rooms); i++)
			if (eaFind(&room_stack, rooms[i]) == -1)
			{
				genesisRaiseError(GENESIS_ERROR, 
					genesisMakeTempErrorContextRoom(rooms[i]->room.name, layout_name), 
					"Room not connected to rest of map.");
			}
	}
	eaDestroy(&room_stack);
}

static void genesisPreTransmogrifyInterior(GenesisInteriorLayout *vague)
{
	if(vague->layout_info_specifier == GenesisTemplateOrCustom_Template) {
		GenesisMapDescInteriorLayoutTemplate *int_template = GET_REF(vague->int_template);
		if(!int_template) {
			genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(vague->name), "Layout Info type is Template, but no template exists.");
			return;
		}
		if(!int_template->backdrop_info) {
			genesisRaiseErrorInternal(GENESIS_FATAL_ERROR, "Interior Layout Template", int_template->name, "Template is missing backdrop information.");
			return;
		}
		if(!int_template->layout_info) {
			genesisRaiseErrorInternal(GENESIS_FATAL_ERROR, "Interior Layout Template", int_template->name, "Template is missing layout information.");
			return;
		}
		
		StructCopyAll(parse_GenesisMapDescBackdrop, int_template->backdrop_info, &vague->common_data.backdrop_info);
		StructCopyAll(parse_GenesisInteriorLayoutInfo, int_template->layout_info, &vague->info);

		if(int_template->detail_kit_1) {
			StructCopyAll(parse_GenesisDetailKitLayout, int_template->detail_kit_1, &vague->detail_kit_1);
		} else {
			StructReset(parse_GenesisDetailKitLayout, &vague->detail_kit_1);
			vague->detail_kit_1.detail_kit_specifier = GenesisTagOrName_SpecificByName;
		}

		if(int_template->detail_kit_2) {
			StructCopyAll(parse_GenesisDetailKitLayout, int_template->detail_kit_2, &vague->detail_kit_2);
		} else {
			StructReset(parse_GenesisDetailKitLayout, &vague->detail_kit_2);
			vague->detail_kit_2.detail_kit_specifier = GenesisTagOrName_SpecificByName;
		}
	}
}

static void genesisTransmogrifyInteriorChallenge(GenesisZoneInterior *concrete, char *room_name, const char *object_name, GenesisMissionChallenge *challenge,
												 int *current_id, int count, GenesisMapDescription *map_desc, int mission_uid, bool exterior, GenesisEncounterJitter *jitter)
{
	genesisTransmogrifyRoomPathChallenge(concrete->layout_name, concrete->rooms, concrete->paths, room_name, object_name, challenge, current_id, count, map_desc, mission_uid, exterior, jitter);
}

static void genesisTransmogrifyInteriorPortal(GenesisZoneInterior *concrete, GenesisMissionPortal *portal, GenesisMapDescription *map_desc, int mission_uid)
{
	genesisTransmogrifyRoomPathPortal(concrete->layout_name, concrete->rooms, concrete->paths, portal, map_desc, mission_uid);
}

void genesisTransmogrifyInterior(U32 seed, U32 detail_seed, GenesisMapDescription *map_desc, GenesisInteriorLayout *vague_in, GenesisZoneInterior *concrete)
{
	int i;
	GenesisInteriorKit *room_kit;
	GenesisInteriorKit *light_kit;
	GenesisInteriorLayout *vague = StructCreate(parse_GenesisInteriorLayout);
	MersenneTable *table = mersenneTableCreate(seed);
	MersenneTable *detail_table = mersenneTableCreate(detail_seed);
	GenesisDetailKitLayout *vague_layout_kits[2] = {0};
	bool first_entrance = true;

	StructCopyAll(parse_GenesisInteriorLayout, vague_in, vague);
	genesisPreTransmogrifyInterior(vague);

	concrete->layout_seed = vague->common_data.layout_seed;

	vague_layout_kits[GDKT_Detail_1] = &vague->detail_kit_1;
	vague_layout_kits[GDKT_Detail_2] = &vague->detail_kit_2;
	vague_layout_kits[GDKT_Detail_1]->random_detail_kit = NULL;
	vague_layout_kits[GDKT_Detail_2]->random_detail_kit = NULL;
	genesisTransmogrifyRooms(vague->rooms, &concrete->rooms, table, detail_table, vague_layout_kits, vague->name);
	genesisTransmogrifyPaths(vague->paths, &concrete->paths, table, detail_table, vague_layout_kits, vague->name);

	StructCopyString( &concrete->layout_name, vague->name );
	concrete->no_sharing_detail = vague->common_data.no_sharing_detail;
	if( concrete->no_sharing_detail && eaSize( &map_desc->shared_challenges ) > 0 ) {
		genesisRaiseError( GENESIS_WARNING, genesisMakeTempErrorContextLayout(vague->name), "NoSharingDetail is true, but there are SharedChallenges.  The SharedChallenges will not be in the same position across different missions." );
	}

	// Verify that all rooms and paths are connected
	{
		int it;
		for( it = 0; it != eaSize( &map_desc->missions ); ++it ) {
			genesisTransmogrifyVerifyLayout(concrete->rooms, concrete->paths, map_desc->missions[it]->zoneDesc.eaPortals, false, vague->name);
		}
	}

	for (i = 0; i < eaSize(&map_desc->missions); i++)
	{
		GenesisRoomMission ***mission_list = NULL;
		
		// Only create a start spawn point for the first mission
		if( i == 0 && stricmp( vague->name, map_desc->missions[i]->zoneDesc.startDescription.pcStartLayout ) == 0 )
		{
			mission_list = genesisRoomFindMissionListByName(concrete->rooms, concrete->paths, map_desc->missions[i]->zoneDesc.startDescription.pcStartRoom);
			if(mission_list && map_desc->missions[i]->zoneDesc.pcName)
			{
				GenesisRoomMission *mission = genesisRoomFindOrMakeMission(mission_list, i, map_desc->missions[i]->zoneDesc.pcName);
				if (map_desc->missions[i]->zoneDesc.startDescription.bHasEntryDoor)
				{
					GenesisInteriorReplace *replace = StructCreate(parse_GenesisInteriorReplace);
					replace->old_tag = StructAllocString("WallReplaceable");
					if (map_desc->missions[i]->zoneDesc.startDescription.eExitFrom == GenesisMissionExitFrom_Entrance)
					{
						if (first_entrance)
							replace->new_tag = StructAllocString("WallStartEntranceExit");
						else
							replace->new_tag = StructAllocString("WallGoToEntranceExit");
					}
					else
					{
						if (first_entrance)
							replace->new_tag = StructAllocString("WallStartEntrance");
						else
							replace->new_tag = StructAllocString("WallGoToEntrance");
					}
					replace->replace_layer = GENESIS_INTERIOR_WALL;
					first_entrance = false;
					replace->is_door = true;
					eaPush(&mission->replaces, replace);
				}
				else
				{
					GenesisObject *new_object = StructCreate(parse_GenesisObject);
					if (IS_HANDLE_ACTIVE(map_desc->missions[i]->zoneDesc.startDescription.hStartTransitionOverride))
					{
						COPY_HANDLE(new_object->start_spawn_using_transition, map_desc->missions[i]->zoneDesc.startDescription.hStartTransitionOverride);
					}
					else
					{
						new_object->obj.name_str = StructAllocString("Start Spawn Point");
					}
					new_object->challenge_name = StructAllocString("Start");
					new_object->params.is_start_spawn = true;
					new_object->source_context = genesisMakeErrorAutoGen( genesisMakeErrorContextDictionary( OBJECT_LIBRARY_DICT, new_object->obj.name_str ) );
					eaPush(&mission->objects, new_object);

					if (map_desc->missions[i]->zoneDesc.startDescription.eExitFrom == GenesisMissionExitFrom_Entrance)
					{
						genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextMission(map_desc->missions[i]->zoneDesc.pcName), "ExitFrom is set to Entrance but mission has no Entrance." );
					}
					if (map_desc->missions[i]->zoneDesc.startDescription.bContinue &&
						map_desc->missions[i]->zoneDesc.startDescription.eContinueFrom == GenesisMissionExitFrom_Entrance)
					{
						genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextMission(map_desc->missions[i]->zoneDesc.pcName), "ContinueFrom is set to Entrance but mission has no Entrance.");
					}
				}
			}
		}
		if (map_desc->missions[i]->zoneDesc.startDescription.eExitFrom == GenesisMissionExitFrom_DoorInRoom
			&& stricmp( vague->name, map_desc->missions[i]->zoneDesc.startDescription.pcExitLayout) == 0)
		{
			mission_list = genesisRoomFindMissionListByName(concrete->rooms, concrete->paths, map_desc->missions[i]->zoneDesc.startDescription.pcExitRoom);
			if(mission_list && map_desc->missions[i]->zoneDesc.pcName)
			{
				GenesisRoomMission *mission = genesisRoomFindOrMakeMission(mission_list, i, map_desc->missions[i]->zoneDesc.pcName);
				GenesisInteriorReplace *replace = StructCreate(parse_GenesisInteriorReplace);
				replace->old_tag = StructAllocString("WallReplaceable");
				replace->new_tag = StructAllocString("WallClickExit");
				replace->replace_layer = GENESIS_INTERIOR_WALL;
				replace->is_door = true;
				eaPush(&mission->replaces, replace);
			}
		}
	}

	mersenneTableFree(detail_table);		

	genesisTransmogrifyRoomChallengesAndPortals(genesisTransmogrifyInteriorChallenge, genesisTransmogrifyInteriorPortal,
												concrete, map_desc, table, false, &vague->common_data.jitter);

	concrete->vert_dir = vague->vert_dir;

	// Room kit (required)
	room_kit = GET_REF(vague->info.room_kit);
	if (!room_kit)
	{
		room_kit = genesisGetRandomItemFromTagList(table, GENESIS_INTERIORS_DICTIONARY, vague->info.room_kit_tag_list, NULL);
	}
	if (!room_kit)
	{
		if (IS_HANDLE_ACTIVE(vague->info.room_kit)) {
			genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(vague->name), "Could not find interior kit \"%s\".", REF_STRING_FROM_HANDLE(vague->info.room_kit));
		} else {
			genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(vague->name), "Could not find interior kit from tags \"%s\".", genesisFillTags(vague->info.room_kit_tag_list));
		}
		mersenneTableFree(table);
		StructDestroy(parse_GenesisInteriorLayout, vague);
		return;
	}
	SET_HANDLE_FROM_REFERENT(GENESIS_INTERIORS_DICTIONARY, room_kit, concrete->room_kit);

	genesisTransmogrifyBackdrop(&vague->common_data.backdrop_info, &concrete->backdrop, table, NULL, room_kit->sound_info, false, vague->name);

	// Light kits
	light_kit = GET_REF(vague->info.light_kit);
	if (!light_kit)
	{
		light_kit = genesisGetRandomItemFromTagList(table, GENESIS_INTERIORS_DICTIONARY, vague->info.light_kit_tag_list, NULL);
	}
	if (!light_kit)
	{
		if(concrete->backdrop && concrete->backdrop->int_light.no_lights)
		{
			if (IS_HANDLE_ACTIVE(vague->info.light_kit)) {
				genesisRaiseError(GENESIS_WARNING, genesisMakeTempErrorContextLayout(vague->name), "Could not find interior light kit \"%s\".", REF_STRING_FROM_HANDLE(vague->info.light_kit));
			} else {
				genesisRaiseError(GENESIS_WARNING, genesisMakeTempErrorContextLayout(vague->name), "Could not find interior light kit from tags \"%s\".", genesisFillTags(vague->info.light_kit_tag_list));
			}
		}
	}
	else
	{
		if(!concrete->backdrop || !concrete->backdrop->int_light.no_lights )
			genesisRaiseError(GENESIS_WARNING, genesisMakeTempErrorContextLayout(vague->name), "You have a light kit and your backdrop allows projector lights.  You probably do not want both.");
		SET_HANDLE_FROM_REFERENT(GENESIS_INTERIORS_DICTIONARY, light_kit, concrete->light_kit);
	}

	mersenneTableFree(table);
	StructDestroy(parse_GenesisInteriorLayout, vague);

	concrete->tmog_version = TMOG_INTERIOR_VERSION;
}


static void genesisPreTransmogrifyExterior(GenesisExteriorLayout *vague)
{
	if(vague->layout_info_specifier == GenesisTemplateOrCustom_Template) {
		GenesisMapDescExteriorLayoutTemplate *ext_template = GET_REF(vague->ext_template);
		if(!ext_template) {
			genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(vague->name), "Layout Info type is Template, but no template exists.");
			return;
		}
		if(!ext_template->backdrop_info) {
			genesisRaiseErrorInternal(GENESIS_FATAL_ERROR, "Exterior Layout Template", ext_template->name, "Template is missing backdrop information." );
			return;
		}
		if(!ext_template->layout_info) {
			genesisRaiseErrorInternal(GENESIS_FATAL_ERROR, "Exterior Layout Template", ext_template->name, "Template is missing layout information." );
			return;
		}

		StructCopyAll(parse_GenesisMapDescBackdrop, ext_template->backdrop_info, &vague->common_data.backdrop_info);
		StructCopyAll(parse_GenesisExteriorLayoutInfo, ext_template->layout_info, &vague->info);

		if(ext_template->detail_kit_1) {
			StructCopyAll(parse_GenesisDetailKitLayout, ext_template->detail_kit_1, &vague->detail_kit_1);
		} else {
			StructReset(parse_GenesisDetailKitLayout, &vague->detail_kit_1);
			vague->detail_kit_1.detail_kit_specifier = GenesisTagOrName_SpecificByName;
		}

		if(ext_template->detail_kit_2) {
			StructCopyAll(parse_GenesisDetailKitLayout, ext_template->detail_kit_2, &vague->detail_kit_2);
		} else {
			StructReset(parse_GenesisDetailKitLayout, &vague->detail_kit_2);
			vague->detail_kit_2.detail_kit_specifier = GenesisTagOrName_SpecificByName;
		}
	}
}

void genesisTransmogrifyExteriorChallenge(GenesisZoneExterior *concrete, char *room_name, const char *object_name, GenesisMissionChallenge *challenge,
										  int *current_id, int count, GenesisMapDescription *map_desc, int mission_uid, bool exterior, GenesisEncounterJitter *jitter)
{
	genesisTransmogrifyRoomPathChallenge(concrete->layout_name, concrete->rooms, concrete->paths, room_name, object_name, challenge, current_id, count, map_desc, mission_uid, exterior, jitter);
}

static void genesisTransmogrifyExteriorPortal(GenesisZoneExterior *concrete, GenesisMissionPortal *portal, GenesisMapDescription *map_desc, int mission_uid)
{
	genesisTransmogrifyRoomPathPortal(concrete->layout_name, concrete->rooms, concrete->paths, portal, map_desc, mission_uid);
}

void genesisTransmogrifyExterior(U32 seed, U32 detail_seed, GenesisMapDescription *map_desc, GenesisExteriorLayout *vague_in, GenesisZoneExterior *concrete)
{
	int i;
	GenesisGeotype *geotype;
	GenesisEcosystem *ecosystem;
	GenesisExteriorLayout *vague = StructCreate(parse_GenesisExteriorLayout);
	MersenneTable *table = mersenneTableCreate(seed);
	MersenneTable *detail_table = mersenneTableCreate(detail_seed);
	GenesisDetailKitLayout *vague_layout_kits[2] = {0};

	StructCopyAll(parse_GenesisExteriorLayout, vague_in, vague);
	genesisPreTransmogrifyExterior(vague);

	concrete->layout_seed = vague->common_data.layout_seed;

	vague_layout_kits[GDKT_Detail_1] = &vague->detail_kit_1;
	vague_layout_kits[GDKT_Detail_2] = &vague->detail_kit_2;
	vague_layout_kits[GDKT_Detail_1]->random_detail_kit = NULL;
	vague_layout_kits[GDKT_Detail_2]->random_detail_kit = NULL;
	genesisTransmogrifyRooms(vague->rooms, &concrete->rooms, table, detail_table, vague_layout_kits, vague->name);
	genesisTransmogrifyPaths(vague->paths, &concrete->paths, table, detail_table, vague_layout_kits, vague->name);

	StructCopyString( &concrete->layout_name, vague->name );
	concrete->no_sharing_detail = vague->common_data.no_sharing_detail;
	if( concrete->no_sharing_detail && eaSize( &map_desc->shared_challenges ) > 0 ) {
		genesisRaiseError( GENESIS_WARNING, genesisMakeTempErrorContextLayout(vague->name), "NoSharingDetail is true, but there are SharedChallenges.  The SharedChallenges will not be in the same position across different missions." );
	}

	for (i = 0; i < eaSize(&map_desc->missions); i++)
	{
		GenesisRoomMission ***mission_list = NULL;
		
		// Only create a start spawn point for the first mission
		if(i==0)
		{
			concrete->start_room = StructAllocString(map_desc->missions[i]->zoneDesc.startDescription.pcStartRoom);
			concrete->end_room = StructAllocString(map_desc->missions[i]->zoneDesc.startDescription.pcExitRoom);
			if(vague->shape != GENESIS_EXT_RAND && !concrete->start_room)
				genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(vague->name), "You must have a start room if you have a shape");
			if(vague->shape != GENESIS_EXT_RAND && !concrete->end_room)
				genesisRaiseError(GENESIS_FATAL_ERROR, genesisMakeTempErrorContextLayout(vague->name), NULL, "You must have a end room if you have a shape");
			
			mission_list = genesisRoomFindMissionListByName(concrete->rooms, concrete->paths, map_desc->missions[i]->zoneDesc.startDescription.pcStartRoom);
			// TODO: Pull this logic out.  Start spawn should only be
			// placed if STARTROOM is the same layout.  Since
			// currently you can only have one layout on exterior
			// maps, this is okay.
			if(mission_list && map_desc->missions[i]->zoneDesc.pcName)
			{
				GenesisRoomMission *mission = genesisRoomFindOrMakeMission(mission_list, i, map_desc->missions[i]->zoneDesc.pcName);
				GenesisZoneMapRoom *start_room = genesisGetRoomByName(concrete->rooms, concrete->start_room);
				GenesisObject *new_object = StructCreate(parse_GenesisObject);
				if (IS_HANDLE_ACTIVE(map_desc->missions[i]->zoneDesc.startDescription.hStartTransitionOverride))
				{
					COPY_HANDLE(new_object->start_spawn_using_transition, map_desc->missions[i]->zoneDesc.startDescription.hStartTransitionOverride);
				}
				else
				{
					new_object->obj.name_str = StructAllocString("Start Spawn Point");
				}
				new_object->challenge_name = StructAllocString("Start");
				new_object->params.is_start_spawn = true;
				if(start_room && start_room->off_map)
				{
					new_object->params.location = GenesisChallengePlace_ExactCenter;
					new_object->params.facing = GenesisChallengeFace_Entrance_Exit;
				}
				new_object->source_context = genesisMakeErrorAutoGen( genesisMakeErrorContextDictionary( OBJECT_LIBRARY_DICT, new_object->obj.name_str ) );
				eaPush(&mission->objects, new_object);
			}
		}
	}

	for (i = 0; i < eaSize(&concrete->rooms); i++)
	{
		concrete->rooms[i]->room.width *= GENESIS_EXTERIOR_KIT_SIZE;
		concrete->rooms[i]->room.depth *= GENESIS_EXTERIOR_KIT_SIZE;
	}

	for (i = 0; i < eaSize(&concrete->paths); i++)
	{
		concrete->paths[i]->path.width *= GENESIS_EXTERIOR_KIT_SIZE;
		concrete->paths[i]->path.min_length *= GENESIS_EXTERIOR_KIT_SIZE;
		concrete->paths[i]->path.max_length *= GENESIS_EXTERIOR_KIT_SIZE;
	}

	{
		//Making a copy of the map desc to fix a bad mistake made that edited the original map desc.
		GenesisMapDescription *map_desc_copy = StructClone(parse_GenesisMapDescription, map_desc);

		genesisExpandSideTrailChallenges(&concrete->rooms, &concrete->paths, map_desc, table, vague->name);

		genesisTransmogrifyRoomChallengesAndPortals(genesisTransmogrifyExteriorChallenge, genesisTransmogrifyExteriorPortal,
													concrete, map_desc, table, true, &vague->common_data.jitter);
		
		StructDestroy(parse_GenesisMapDescription, map_desc_copy);
	}

	geotype = GET_REF(vague->info.geotype);
	if(!geotype)
	{
		geotype = genesisGetRandomItemFromTagList(table, GENESIS_GEOTYPE_DICTIONARY, vague->info.geotype_tag_list, NULL);
	}
	if(geotype)
	{
		SET_HANDLE_FROM_REFERENT(GENESIS_GEOTYPE_DICTIONARY, geotype, concrete->geotype);
	}
	else
	{
		genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(vague->name), "No Geotype specified for Exterior Layout");
	}

	ecosystem = GET_REF(vague->info.ecosystem);
	if(!ecosystem)
	{
		ecosystem = genesisGetRandomItemFromTagList(table, GENESIS_ECOTYPE_DICTIONARY, vague->info.ecosystem_tag_list, NULL);
	}
	if(ecosystem)
	{
		SET_HANDLE_FROM_REFERENT(GENESIS_ECOTYPE_DICTIONARY, ecosystem, concrete->ecosystem);
	}
	else
	{
		genesisRaiseError(GENESIS_ERROR, genesisMakeTempErrorContextLayout(vague->name), "No Ecosystem specified for Exterior Layout");
	}

	if(ecosystem && geotype)
	{
		int vista_cnt=0;
		char pub_name[MAX_PATH];
		ZoneMapInfo **zmap_list = NULL;
		sprintf(pub_name, "%s_%s_%s_", GENESIS_EXTERIOR_VISTAS_FOLDER, ecosystem->name, geotype->name);
		worldGetZoneMapsThatStartWith(pub_name, &zmap_list);
		vista_cnt = eaSize(&zmap_list);
		if(vista_cnt > 0) {
			ZoneMapInfo *vista_zmap = zmap_list[randomMersenneU32(table)%vista_cnt];
			concrete->vista_map = allocAddString(zmapInfoGetPublicName(vista_zmap));
		} else {
			genesisRaiseError(GENESIS_WARNING, genesisMakeTempErrorContextLayout(vague->name), "The Ecosystem, Geotype combination \"%s, %s\" does not have a vista.", ecosystem->name, geotype->name);
			concrete->vista_map = NULL;
		}
		eaDestroy(&zmap_list);
	}

	genesisTransmogrifyBackdrop(&vague->common_data.backdrop_info, &concrete->backdrop, table, NULL, SAFE_MEMBER(ecosystem, sound_info), true, vague->name);

	concrete->color_shift = vague->info.color_shift;
	copyVec2(vague->play_min, concrete->play_min);
	copyVec2(vague->play_max, concrete->play_max);
	concrete->play_buffer = vague->play_buffer;
	concrete->vert_dir = vague->vert_dir;
	concrete->shape = vague->shape;
	concrete->max_road_angle = vague->max_road_angle;
	concrete->is_vista = vague->is_vista;
	concrete->tmog_version = TMOG_EXTERIOR_VERSION;

	mersenneTableFree(table);
	mersenneTableFree(detail_table);
	StructDestroy(parse_GenesisExteriorLayout, vague);
}

//////////////////////////////////////////////////////////////////
// Volume creation utility
//////////////////////////////////////////////////////////////////

GenesisToPlaceObject *genesisCreateChallengeVolume(GenesisToPlaceObject *parent_object, GenesisProceduralObjectParams* volume_params,
								  GroupDef *orig_def, GenesisToPlaceObject *orig_object, GenesisRuntimeErrorContext *parent_context, GenesisObjectVolume *volume)
{
	char volume_str[256];
	F32 size = volume->size;
	GenesisToPlaceObject *to_place_object = calloc(1, sizeof(GenesisToPlaceObject));
	to_place_object->uid = 0;

	sprintf(volume_str, "%s_VOLUME", orig_object->challenge_name);
	to_place_object->challenge_name = strdup(volume_str);
	to_place_object->object_name = strdup(volume_str);
	to_place_object->challenge_is_unique = true;
	to_place_object->force_named_object = true;
	copyMat4(orig_object->mat, to_place_object->mat);
	to_place_object->parent = parent_object;
	to_place_object->seed = 0;
	to_place_object->source_context = StructClone( parse_GenesisRuntimeErrorContext, parent_context );

	to_place_object->params = StructCreate(parse_GenesisProceduralObjectParams);

	StructCopyAll( parse_GenesisProceduralObjectParams, volume_params, to_place_object->params );

	if (!to_place_object->params->volume_properties)
		to_place_object->params->volume_properties = StructCreate(parse_GroupVolumeProperties);

	if (volume->pVolumeProperties)
	{
		to_place_object->params->volume_properties->eShape = volume->pVolumeProperties->eShape;
		copyVec3(volume->pVolumeProperties->vBoxMin, to_place_object->params->volume_properties->vBoxMin);
		copyVec3(volume->pVolumeProperties->vBoxMax, to_place_object->params->volume_properties->vBoxMax);
		to_place_object->params->volume_properties->fSphereRadius = volume->pVolumeProperties->fSphereRadius;
	}
	else if (volume->is_relative)
	{
		if (volume->is_square)
		{
			to_place_object->params->volume_properties->eShape = GVS_Box;
			scaleVec3(orig_def->bounds.min, size, to_place_object->params->volume_properties->vBoxMin);
			scaleVec3(orig_def->bounds.max, size, to_place_object->params->volume_properties->vBoxMax);
		}
		else
		{
			to_place_object->params->volume_properties->eShape = GVS_Sphere;
			to_place_object->params->volume_properties->fSphereRadius = orig_def->bounds.radius*size;
		}
	}
	else
	{
		if (volume->is_square)
		{
			to_place_object->params->volume_properties->eShape = GVS_Box;
			setVec3same(to_place_object->params->volume_properties->vBoxMin, -size);
			setVec3same(to_place_object->params->volume_properties->vBoxMax,  size);
		}
		else
		{
			to_place_object->params->volume_properties->eShape = GVS_Sphere;
			to_place_object->params->volume_properties->fSphereRadius = size;
		}
	}
	//eaPushUnique(&to_place_object->params->volume_properties->ppcVolumeTypes, allocAddString("Interaction"));

	return to_place_object;
}

//////////////////////////////////////////////////////////////////
// GroupDef creation
//////////////////////////////////////////////////////////////////

void genesisBuildObjectPath(GenesisToPlaceObject *object, char *path, int path_size)
{
	char tmp[10];
	if (object->parent)
		genesisBuildObjectPath(object->parent, path, path_size);
	sprintf(tmp, "%d,", object->uid_in_parent);
	strcat_s(path, path_size, tmp);
}

GroupDef *genesisInstancePath(GroupDefLib *def_lib, GroupDef *def, char *path)
{
	int *pathIndexes = groupDefScopeGetIndexesFromPath(def, path);
	char new_name[128];
		
	GroupDef *defIt = def;
	int it;
	for( it = 0; it != eaiSize( &pathIndexes ); ++it ) {
		GroupChild *child = defIt->children[ pathIndexes[ it ]];
		GroupDef *child_def = groupChildGetDef(defIt, child, false);
		GroupDef *instanced;

		assert(child_def);
		groupLibMakeGroupName(def_lib, child_def->name_str, SAFESTR(new_name), 0);
		instanced = groupLibCopyGroupDef(def_lib, NULL, child_def, new_name, false, true, false, 0, false);
			
		defIt = child_def;
	}

	eaiDestroy(&pathIndexes);
	return defIt;
}

void genesisDestroyToPlaceObject(GenesisToPlaceObject *object)
{
	SAFE_FREE(object->challenge_name);
	StructDestroy(parse_GenesisProceduralObjectParams, object->params);
	if(object->instanced)
	{
		StructDestroy(parse_GenesisInstancedObjectParams, object->instanced);
	}
	SAFE_FREE(object);
}

void genesisDestroyToPlacePlatformGroup(GenesisToPlacePlatformGroup *group)
{
	exclusionGridFree(group->platform_grid);
	SAFE_FREE(group);
}

GenesisToPlacePatrol* genesisCloneToPlacePatrol(GenesisToPlacePatrol *patrol)
{
	GenesisToPlacePatrol* accum = calloc( 1, sizeof( *accum ));
	accum->patrol_name = strdup( patrol->patrol_name );
	StructCopyAll( parse_WorldPatrolProperties, &patrol->patrol_properties, &accum->patrol_properties );

	return accum;
}

void genesisDestroyToPlacePatrol(GenesisToPlacePatrol *patrol)
{
	StructFreeStringSafe( &patrol->patrol_name );
	StructDeInit( parse_WorldPatrolProperties, &patrol->patrol_properties );
	SAFE_FREE(patrol);
}

void genesisApplyObjectParams(GroupDef *def, GenesisProceduralObjectParams *params)
{
	if (params->model_name)
		def->model = modelFind(params->model_name, false, WL_FOR_WORLD);
	if (params->action_volume_properties)
		def->property_structs.server_volume.action_volume_properties = StructClone(parse_WorldActionVolumeProperties, params->action_volume_properties);
	if (params->event_volume_properties)
		def->property_structs.server_volume.event_volume_properties = StructClone(parse_WorldEventVolumeProperties, params->event_volume_properties);
	if (params->optionalaction_volume_properties)
		def->property_structs.server_volume.obsolete_optionalaction_properties = StructClone(parse_WorldOptionalActionVolumeProperties, params->optionalaction_volume_properties);
	if (params->fx_volume)
		def->property_structs.client_volume.fx_volume_properties = StructClone(parse_WorldFXVolumeProperties, params->fx_volume);
	if (params->sky_volume_properties)
		def->property_structs.client_volume.sky_volume_properties = StructClone(parse_WorldSkyVolumeProperties, params->sky_volume_properties);
	if (params->sound_volume_properties)
		def->property_structs.client_volume.sound_volume_properties = StructClone(parse_WorldSoundVolumeProperties, params->sound_volume_properties);
	if (params->power_volume)
		def->property_structs.server_volume.power_volume_properties = StructClone(parse_WorldPowerVolumeProperties, params->power_volume);
	if (params->curve)
		def->property_structs.curve = StructClone(parse_WorldCurve, params->curve);
	if (params->patrol_properties)
		def->property_structs.patrol_properties = StructClone(parse_WorldPatrolProperties, params->patrol_properties);
	if (params->interaction_properties)
		def->property_structs.interaction_properties = StructClone(parse_WorldInteractionProperties, params->interaction_properties);
	if (params->spawn_properties)
		def->property_structs.spawn_properties = StructClone(parse_WorldSpawnProperties, params->spawn_properties);
	StructCopyAll(parse_WorldPhysicalProperties, &params->physical_properties, &def->property_structs.physical_properties);
	StructCopyAll(parse_WorldTerrainProperties, &params->terrain_properties, &def->property_structs.terrain_properties);
	if (params->volume_properties)
		def->property_structs.volume = StructClone(parse_GroupVolumeProperties, params->volume_properties);
	if (params->hull_properties)
		def->property_structs.hull = StructClone(parse_GroupHullProperties, params->hull_properties);
	if (params->light_properties)
		def->property_structs.light_properties = StructClone(parse_WorldLightProperties, params->light_properties);
	if (params->genesis_properties)
		def->property_structs.genesis_properties = StructClone(parse_WorldGenesisProperties, params->genesis_properties);
	if (params->room_properties)
		def->property_structs.room_properties = StructClone(parse_WorldRoomProperties, params->room_properties);
	if (params->sound_sphere_properties)
		def->property_structs.sound_sphere_properties = StructClone(parse_WorldSoundSphereProperties, params->sound_sphere_properties);
}

//If you change the order of things in this function, you must also change exclusionGetDefVolumes
void genesisApplyActorData(GenesisInstancedChildParams ***ea_actor_data, GroupDef *challenge_def, const Mat4 parent_mat)
{
	int i, j;

	if(challenge_def->property_structs.encounter_properties)
	{
		WorldEncounterProperties *enc_props = challenge_def->property_structs.encounter_properties;
		for ( j=0; j < eaSize(&enc_props->eaActors); j++ )
		{
			GenesisInstancedChildParams *actor_data;
			WorldActorProperties *actor = enc_props->eaActors[j];

			actor_data = eaGet(ea_actor_data, j);

			if(!actor_data)
				continue;

			copyVec3(actor_data->vOffset, actor->vPos);
			copyVec3(actor_data->vPyr, actor->vRot);

			if(!actor->displayNameMsg.pEditorCopy || nullStr(actor->displayNameMsg.pEditorCopy->pcDefaultString))
				StructCopyAll(parse_DisplayMessage, &actor_data->displayNameMsg, &actor->displayNameMsg);

			if(actor_data->pCostumeProperties)
			{
				if(!actor->pCostumeProperties)
					actor->pCostumeProperties = StructCreate(parse_WorldActorCostumeProperties);

				StructCopyAll(parse_WorldActorCostumeProperties, actor_data->pCostumeProperties, actor->pCostumeProperties);
			}
		}
		eaDestroyStruct(ea_actor_data, parse_GenesisInstancedChildParams);
	}

	for (i = 0; i < eaSize(&challenge_def->children); i++)
	{
		GroupChild *child = challenge_def->children[i];
		GroupDef *child_def = groupChildGetDef(challenge_def, child, false);
		if (child_def)
		{
			Mat4 child_mat;
			mulMat4(parent_mat, child->mat, child_mat);
			genesisApplyActorData(ea_actor_data, child_def, child_mat);
		}
	}
}

bool genesisObjectIsChallenge(GenesisToPlaceObject* object)
{
	WorldGenesisChallengeProperties* genesisProperties = object->group_def->property_structs.genesis_challenge_properties;

	{
		// Check for UGC tags
		if (object->group_def->tags && strstri(object->group_def->tags, "UGC") != NULL)
		{
			return true;
		}
	}

	return (genesisProperties && genesisProperties->type != GenesisChallenge_None);
}

bool genesisObjectIsPortal(GenesisToPlaceObject* object)
{
	WorldSpawnProperties* spawnProperties = object->group_def ? object->group_def->property_structs.spawn_properties : NULL;

	return (spawnProperties && spawnProperties->spawn_type == SPAWNPOINT_GOTO);
}

static bool isZeroVec3( Vec3 vec )
{
	return vec[0] == 0 && vec[1] == 0 && vec[2] == 0;
}

static GenesisToPlacePatrol* genesisCombinePatrolListAndFree( GenesisToPlacePatrol*** patrolList )
{
	GenesisToPlacePatrol* accum = (*patrolList)[0];
	eaRemove( patrolList, 0 );

	// All the patrols are segments that can be combined into a single
	// path.  So to combine them, I can just find all the segments
	// that would be placed immediately after this, and all the ones
	// that'd go immediately before it.

	// Find all going after
	{
		bool foundEnd;
		int otherIt;

		do {
			foundEnd = false;
			for( otherIt = 0; otherIt != eaSize( patrolList ); ++otherIt ) {
				GenesisToPlacePatrol* other = (*patrolList)[ otherIt ];
				WorldPatrolPointProperties** accumPoints = accum->patrol_properties.patrol_points;
				WorldPatrolPointProperties** otherPoints = other->patrol_properties.patrol_points;
				float* accumEnd;
				float* otherStart;

				assert( eaSize( &accumPoints ) > 0 );
				assert( eaSize( &otherPoints ) > 0 );

				accumEnd = accumPoints[ eaSize( &accumPoints ) - 1 ]->pos;
				otherStart = otherPoints[ 0 ]->pos;

				if( distance3( accumEnd, otherStart ) < 40.0 ) {
					eaPushEArray( &accum->patrol_properties.patrol_points, &other->patrol_properties.patrol_points );
					eaClear( &other->patrol_properties.patrol_points );
					genesisDestroyToPlacePatrol( other );
					eaRemove( patrolList, otherIt );
					foundEnd = true;
					break;
				}
			}
		} while( foundEnd );
	}

	// Find all going before
	{
		bool foundEnd;
		int otherIt;

		do {
			foundEnd = false;
			for( otherIt = 0; otherIt != eaSize( patrolList ); ++otherIt ) {
				GenesisToPlacePatrol* other = (*patrolList)[ otherIt ];
				WorldPatrolPointProperties** accumPoints = accum->patrol_properties.patrol_points;
				WorldPatrolPointProperties** otherPoints = other->patrol_properties.patrol_points;
				float* accumStart;
				float* otherEnd;

				assert( eaSize( &accumPoints ) > 0 );
				assert( eaSize( &otherPoints ) > 0 );
				accumStart = accumPoints[ 0 ]->pos;
				otherEnd = otherPoints[ eaSize( &otherPoints ) - 1 ]->pos;

				if( distance3( accumStart, otherEnd ) < 40.0 ) {
					eaInsertEArray( &accum->patrol_properties.patrol_points, &other->patrol_properties.patrol_points, 0 );
					eaClear( &other->patrol_properties.patrol_points );
					genesisDestroyToPlacePatrol( other );
					eaRemove( patrolList, otherIt );
					foundEnd = true;
					break;
				}
			}
		} while( foundEnd );
	}

	// Could not combine all of the patrols, probably because placement errors happened.
	if( eaSize( patrolList )) {
		genesisRaiseError( GENESIS_ERROR, (*patrolList)[0]->source_context,
						   "INTERNAL ERROR - Could not connect all segments of the patrols." );
	}
	eaDestroyEx( patrolList, genesisDestroyToPlacePatrol );

	return accum;
}

bool genesisGetBoundingVolumeFromPoints(GenesisBoundingVolume* out_boundingVolume, F32 *points)
{
	int i;
	F32 angle, dist_sum;
	F32 best_angle = -1.f, best_dist_sum;
	F32 best_max_width=0, best_max_depth=0;
	Vec3 min = { 30000,  30000,  30000};
	Vec3 max = {-30000, -30000, -30000};
	F32 ratio;
	
	setVec3(out_boundingVolume->center, -9001, -9001, -9001);
	zeroVec3(out_boundingVolume->extents[0]);
	zeroVec3(out_boundingVolume->extents[1]);
	out_boundingVolume->rot = 0;

	if(eafSize(&points) == 0) {
		return false;
	}

	assert(eafSize(&points)%3 == 0);

	//Find the mid point
	for ( i=0; i < eafSize(&points); i+=3 )
	{
		Vec3 point;
		point[0] = points[i+0];
		point[1] = points[i+1];
		point[2] = points[i+2];
		MINVEC3(point, min, min);
		MAXVEC3(point, max, max);
	}
	addVec3(min, max, out_boundingVolume->center);
	scaleVec3(out_boundingVolume->center, 0.5f, out_boundingVolume->center);

	//Set vertical portion of extents
	out_boundingVolume->extents[0][1] = min[1] - out_boundingVolume->center[1] - 1;
	out_boundingVolume->extents[1][1] = max[1] - out_boundingVolume->center[1] + 1;

	// Find the best angle
	best_dist_sum = FLT_MAX;
	for ( angle = 0; angle < (180-15); angle += 15)
	{
		Vec2 angle_vec = {cos(RAD(angle)), sin(RAD(angle))};
		Vec2 angle_vec_norm = {-angle_vec[1], angle_vec[0]};
		F32 max_width=0, max_depth=0;

		dist_sum = 0;
		for ( i=0; i < eafSize(&points); i+=3 )
		{
			Vec2 point_vec = {points[i+0] - out_boundingVolume->center[0], points[i+2] - out_boundingVolume->center[2]};
			F32 dist_from_line = ABS(dotVec2(angle_vec_norm, point_vec));
			F32 dist_from_mid = ABS(dotVec2(angle_vec, point_vec));

			dist_sum += dist_from_line;

			if(dist_from_line > max_depth)
				max_depth = dist_from_line;
			if(dist_from_mid > max_width)
				max_width = dist_from_mid;			
		}
		if(dist_sum < best_dist_sum)
		{
			best_dist_sum = dist_sum;
			best_angle = angle;
			best_max_width = max_width;
			best_max_depth = max_depth;
		}
	}
	assert(best_angle >= 0);//We better have found an angle

	//Expand the ellipse
	//This basically finds the square that surrounds the circle that surrounds the square of half width = best_max_width
	//Then it stretches the square to be proportional to the original rectangle
	best_max_width = MAX(best_max_width, 50.0f);
	best_max_depth = MAX(best_max_depth, 50.0f);
	ratio = best_max_depth/best_max_width;
	out_boundingVolume->extents[1][0] = sqrt(SQR(best_max_width)*2);
	out_boundingVolume->extents[0][0] = -out_boundingVolume->extents[1][0];
	out_boundingVolume->extents[1][2] = out_boundingVolume->extents[1][0]*ratio;
	out_boundingVolume->extents[0][2] = -out_boundingVolume->extents[1][2];

	//Setup the mat
	out_boundingVolume->rot = RAD(-best_angle);

	return true;
}

GenesisToPlaceObject *genesisMakeToPlaceObject(const char *name, GenesisToPlaceObject *parent_object, const F32 *pos, GenesisToPlaceState *to_place)
{
	GenesisToPlaceObject *child_object = calloc(1, sizeof(GenesisToPlaceObject));
	if (name)
		child_object->object_name = allocAddString(name);
	identityMat3(child_object->mat);
	if (pos)
		copyVec3(pos, child_object->mat[3]);
	child_object->parent = parent_object;
	eaPush(&to_place->objects, child_object);
	return child_object;
}

void genesisPopulateWaypointVolumes(GenesisToPlaceState *to_place, GenesisMissionRequirements **mission_reqs)
{
	int i, j;
	GenesisToPlaceObject *waypoint_group;

	waypoint_group = genesisMakeToPlaceObject("Waypoints", NULL, zerovec3, to_place);

	for ( i=0; i < eaSize(&mission_reqs); i++ )
	{
		GenesisMissionVolumePoints **volume_points_list = NULL;
		GenesisMissionRequirements *req = mission_reqs[i];

		genesisCalcMissionVolumePointsInto(&volume_points_list, req, to_place);

		for ( j=0; j < eaSize(&volume_points_list); j++ )
		{
			GenesisMissionVolumePoints *volume_info = volume_points_list[j];
			GenesisToPlaceObject *volume_obj;
			GenesisBoundingVolume volume = { 0 };

			volume_obj = calloc(1, sizeof(GenesisToPlaceObject));
			volume_obj->object_name = allocAddString(volume_info->volume_name);
			volume_obj->challenge_name = strdup(volume_info->volume_name);
			volume_obj->challenge_is_unique = true;
			volume_obj->uid = 0;
			volume_obj->parent = waypoint_group;

			if(genesisGetBoundingVolumeFromPoints(&volume, volume_info->positions)) {
				copyMat4(unitmat, volume_obj->mat);
				copyVec3(volume.center, volume_obj->mat[3]);
				yawMat3(volume.rot, volume_obj->mat);
				
				volume_obj->params = StructCreate(parse_GenesisProceduralObjectParams);
				genesisProceduralObjectSetEventVolume(volume_obj->params);
				volume_obj->params->volume_properties = StructCreate(parse_GroupVolumeProperties);
				volume_obj->params->volume_properties->eShape = GVS_Box;
				copyVec3(volume.extents[0], volume_obj->params->volume_properties->vBoxMin);
				copyVec3(volume.extents[1], volume_obj->params->volume_properties->vBoxMax);
				eaPush(&to_place->objects, volume_obj);
			} else {
				free(volume_obj->challenge_name);
				free(volume_obj);

				genesisRaiseErrorInternalCode(GENESIS_ERROR, "Waypoint: %s -- Could not place waypoint.",
											  volume_info->volume_name);
			}
		}

		eaDestroyStruct(&volume_points_list, parse_GenesisMissionVolumePoints);
	}
}

void genesisPopulateWaypointVolumesAsError(GenesisToPlaceState *to_place, GenesisMissionRequirements **mission_reqs, GenesisRuntimeErrorType errorType)
{
	int i;

	for ( i=0; i < eaSize(&mission_reqs); i++ )
	{
		if( eaSize( &mission_reqs[i]->extraVolumes )) {
			genesisRaiseError( errorType, genesisMakeTempErrorContextMission(mission_reqs[i]->missionName),
							   "Mission: %s -- Map type does not usually have waypoints.", mission_reqs[i]->missionName );
		}
	}
}

void genesisSetInternalObjectLogicalName(GenesisToPlaceObject *object, const char *external_name, const char *internal_name, const char *internal_path, GroupDef *root_def, LogicalGroup *group)
{
	// Set external name
	char path[256] = { 0 };
	genesisBuildObjectPath(object, path, 256);
	assert(internal_path || internal_name);
	if (internal_path)
	{
		strcat(path, internal_path);
	}
	else if (internal_name && stricmp(internal_name, "") != 0)
	{
		strcatf(path, "%s,", internal_name);
	}
	groupDefScopeSetPathName(root_def, path, strdup(external_name), false);
	if (!groupDefScopeIsNameUsed(root_def, external_name))
		genesisRaiseErrorInternal(GENESIS_ERROR, OBJECT_LIBRARY_DICT, object->group_def->name_str,
		"Object library piece specifies it has a child group with logical path \"%s\", but no child group was found.", path );

	if (group)
	{
		// Add to logical group
		eaPush(&group->child_names, StructAllocString(external_name));
	}
}

static void genesisForceNamedObject(GroupDef *group_def)
{
	WorldInteractionPropertyEntry* entry = StructCreate( parse_WorldInteractionPropertyEntry );
	entry->pcInteractionClass = allocAddString( "NamedObject" );
	if (group_def->property_structs.volume && !group_def->property_structs.volume->bSubVolume)
	{
		if (!group_def->property_structs.server_volume.interaction_volume_properties)
			group_def->property_structs.server_volume.interaction_volume_properties = StructCreate( parse_WorldInteractionProperties );
		eaPush(&group_def->property_structs.server_volume.interaction_volume_properties->eaEntries, entry);
	}
	else
	{
		if (!group_def->property_structs.interaction_properties)
			group_def->property_structs.interaction_properties = StructCreate( parse_WorldInteractionProperties );
		eaPush(&group_def->property_structs.interaction_properties->eaEntries, entry);
	}
}

void genesisPlaceObjects(ZoneMapInfo *zmap_info, GenesisToPlaceState *to_place, GroupDef *root_def)
{
	int i, j;
	MersenneTable *random_table = NULL;
	
	if(zmap_info && zmap_info->genesis_data)
		random_table = mersenneTableCreate(zmap_info->genesis_data->detail_seed);

	////////////////////////////////////////////////////////
	/// 1. Put patrols into the objects list
	{
		GenesisToPlaceObject* patrol_parent = calloc(1, sizeof(*patrol_parent));
		patrol_parent->object_name = allocAddString("Patrol Routes");
		identityMat4(patrol_parent->mat);
		eaPush(&to_place->objects, patrol_parent);

		// combine all patrols with the same name
		for (i = 0; i < eaSize(&to_place->patrols); i++) {
			GenesisToPlacePatrol *patrol = to_place->patrols[i];
			GenesisToPlacePatrol **other_patrols = NULL;
			
			for (j = eaSize(&to_place->patrols) - 1; j > i; --j) {
				GenesisToPlacePatrol *other_patrol = to_place->patrols[j];

				if( stricmp( patrol->patrol_name, other_patrol->patrol_name ) == 0 ) {
					eaPush( &other_patrols, other_patrol );
					eaRemove( &to_place->patrols, j );
				}
			}

			if( eaSize( &other_patrols )) {
				eaPush( &other_patrols, patrol );
				to_place->patrols[i] = genesisCombinePatrolListAndFree( &other_patrols );
			}
		}

		// convert patrols into objects
		for (i = 0; i < eaSize(&to_place->patrols); i++)
		{
			GenesisToPlacePatrol *patrol = to_place->patrols[i];
			GenesisToPlaceObject *object = calloc(1, sizeof(*object));
			object->challenge_name = StructAllocString(patrol->patrol_name);
			object->challenge_is_unique = true;
			object->parent = patrol_parent;
			identityMat4(object->mat);
			object->params = StructCreate(parse_GenesisProceduralObjectParams);
			object->params->patrol_properties = StructClone( parse_WorldPatrolProperties, &patrol->patrol_properties );
			eaPush(&to_place->objects, object);
		}
	}

	////////////////////////////////////////////////////////
	/// 2. Put all the objects into layers.
	for (i = 0; i < eaSize(&to_place->objects); i++)
	{
		int idx;
		bool force_named = false;
		GenesisToPlaceObject *object = to_place->objects[i];
		GroupDef *parent = object->parent ? object->parent->group_def : root_def;
		char *internal_path = NULL;
		char *spawn_internal_path = NULL;
		char *external_name = NULL;

		assert (parent && parent->def_lib->zmap_layer);
		idx = eaSize(&parent->children);
		object->uid_in_parent = 1;
		for (j = 0; j < eaSize(&parent->children); j++)
			if (parent->children[j]->uid_in_parent >= object->uid_in_parent)
				object->uid_in_parent = parent->children[j]->uid_in_parent+1;
		eaPush(&parent->children, StructCreate(parse_GroupChild));
		parent->children[idx]->uid_in_parent = object->uid_in_parent;
		if (!object->group_def)
			object->group_def = objectLibraryGetGroupDef(object->uid, true);
		if (!object->group_def)
		{
			object->group_def = groupLibFindGroupDef(root_def->def_lib, object->uid, false);
		}
		if (!object->group_def)
		{
			// if uid is 0, then we create a new def from scratch
			char groupName[256];
			GroupDefLib *def_lib = root_def->def_lib;
			groupLibMakeGroupName(def_lib, object->object_name, SAFESTR(groupName), 0);
			object->group_def = groupLibNewGroupDef(def_lib, root_def->filename, 0, groupName, 0, false, true);
			groupDefModify(object->group_def, UPDATE_GROUP_PROPERTIES, true);
		}

		// Generate the external name for this object
		if (object->challenge_name) {
			size_t external_name_size = strlen(object->challenge_name)+4;
			external_name = calloc(1, external_name_size);

			if (!object->challenge_is_unique && !genesisObjectIsPortal(object))
			{
				sprintf_s(SAFESTR2(external_name), "%s_%02d", object->challenge_name, object->challenge_index);
			}
			else
			{
				object->challenge_index = 0;
				strcpy_s(external_name, external_name_size, object->challenge_name);
			}
		}
		
		if (object->group_def->property_structs.genesis_challenge_properties && object->group_def->property_structs.genesis_challenge_properties->spawn_name)
		{
			char *child_path;
			stashFindPointer(object->group_def->name_to_path, object->group_def->property_structs.genesis_challenge_properties->spawn_name, &child_path);
			strdup_alloca(spawn_internal_path, child_path);
		}

		if ((object->force_named_object || genesisObjectIsChallenge(object))
			&& !groupDefNeedsUniqueName(object->group_def))
		{
			force_named = true;
		}
		
		if (!groupIsObjLib(object->group_def))
		{
			if (object->params)
			{
				genesisApplyObjectParams(object->group_def, object->params);
			}
			if (force_named)
			{
				genesisForceNamedObject(object->group_def);
			}
			if (object->instanced)
			{
				if (genesis_instance_fn)
					genesis_instance_fn(zmapInfoGetPublicName(zmap_info), object->group_def, object->instanced, object->interact, external_name, object->source_context);
			}
		}
		else if (object->instanced || force_named)
		{
			// We have to instance this GroupDef.
			char new_name[128];
			GroupDef *new_def, *challenge_def;
			char *complete_name;

			groupLibMakeGroupName(root_def->def_lib, object->group_def->name_str, SAFESTR(new_name), 0);
			new_def = groupLibCopyGroupDef(root_def->def_lib, root_def->filename, object->group_def, new_name, false, true, false, 0, false);
			challenge_def = new_def;
			
			if (object->group_def->property_structs.genesis_challenge_properties && object->group_def->property_structs.genesis_challenge_properties->complete_name)
				complete_name = object->group_def->property_structs.genesis_challenge_properties->complete_name;
			else
				complete_name =  SAFE_MEMBER(object->group_def->property_structs.genesis_properties, pcGenesisCompleteName);
			if (complete_name)
			{
				GroupDef *instanced_def;
				char *child_path;
				if (stashFindPointer(object->group_def->name_to_path, complete_name, &child_path) &&
					(instanced_def = genesisInstancePath(root_def->def_lib, new_def, child_path))) {
					challenge_def = instanced_def;
					strdup_alloca(internal_path, child_path);
				} else {
					genesisRaiseErrorInternal(GENESIS_ERROR, "ObjectLibrary", object->group_def->name_str,
											  "Attempt to instance child \"%s\", but no such child group found.",
											  complete_name );
				}
			}

			stashTableDestroy(new_def->name_to_path);
			stashTableDestroy(new_def->path_to_name);
			new_def->name_to_path = new_def->path_to_name = NULL;
			eaDestroyStruct(&new_def->logical_groups, parse_LogicalGroup);
			// don't need to fixup messages; since they can't have been changed.
			// groupDefFixupMessages(new_def);
			
			if (object->instanced)
			{
				if (!nearSameVec3(object->instanced->model_scale, zerovec3))
				{
					copyVec3(object->instanced->model_scale, challenge_def->model_scale);
				}

				//Apply Encounter Positions
				if(eaSize(&object->instanced->eaChildParams))
				{
					assert( !object->instanced->bChildParamsAreGroupDefs );
					genesisApplyActorData(&object->instanced->eaChildParams, challenge_def, object->mat);
					assert(!object->instanced->eaChildParams);
				}

				if( object->instanced->pcFSMName ) {
					WorldEncounterProperties *enc_props = challenge_def->property_structs.encounter_properties;
					int it;
					for( it = 0; it != eaSize( &enc_props->eaActors ); ++it ) {
						WorldActorProperties *actor = enc_props->eaActors[it];
						SET_HANDLE_FROM_STRING(gFSMDict, object->instanced->pcFSMName, actor->hFSMOverride);
					}
				}

				if (genesis_instance_fn)
					genesis_instance_fn(zmapInfoGetPublicName(zmap_info), challenge_def, object->instanced, object->interact, external_name, object->source_context);
			}

			if (object->params)
			{
				genesisApplyObjectParams(new_def, object->params);
			}

			if (force_named)
			{
				genesisForceNamedObject(new_def);
			}

			object->group_def = new_def;
		}
		//parent->children[idx]->def = object->group_def;
		if(!isNonZeroMat3(object->mat))
			genesisRaiseErrorInternalCode( GENESIS_FATAL_ERROR, "Object: %s -- Rotation matrix that is all zeros, please find a programmer.", object->object_name );
		copyMat4(object->mat, parent->children[idx]->mat);
		if (!object->mat_relative && object->parent)
		{
			// TomY TODO multiply by parent's inverse rotation
			// If you do this, then make it a separate flag, as code is relying on only the position being relative

			// NOTE: This setup would never have worked, but there may
			// be code depending on the current bug state.
			GenesisToPlaceObject* parentIt = object->parent;
			do {
				if( parentIt->mat_relative && !isZeroVec3( parentIt->mat[3] )) {
					genesisRaiseErrorInternalCode( GENESIS_ERROR, "Object: %s -- This uses absolute positioning, but all of its parents do not.", object->object_name );
					break;
				}
				parentIt = parentIt->parent;
			} while( parentIt );
			
			subVec3(object->mat[3], object->parent->mat[3], parent->children[idx]->mat[3]);
		}

		parent->children[idx]->name = allocAddString(object->group_def->name_str);
		parent->children[idx]->name_uid = object->group_def->name_uid;
		if(!object->seed && random_table)
			parent->children[idx]->seed = randomMersenneU32(random_table);
		else
			parent->children[idx]->seed = object->seed;
		parent->children[idx]->scale = object->scale;
		groupSetBounds(parent, false);

		if (wl_state.genesis_error_on_encounter1)
		{
			if( object->group_def->property_structs.encounter_hack_properties ) {
				genesisRaiseError(GENESIS_FATAL_ERROR, object->source_context,
								  "Encounter 1 objects are not allowed.");
			}
		}

		if (object->challenge_name && 
			(force_named || genesisObjectIsChallenge(object) || groupDefNeedsUniqueName(object->group_def) || genesisObjectIsPortal(object) || object->group_def->property_structs.server_volume.action_volume_properties || object->group_def->property_structs.server_volume.event_volume_properties ||
			(object->params && (object->params->action_volume_properties || object->params->event_volume_properties || object->params->patrol_properties))))
		{
			LogicalGroup *group;
			char *internal_name = NULL;

			if( !internal_name && object->group_def->property_structs.genesis_challenge_properties ) {
				internal_name = object->group_def->property_structs.genesis_challenge_properties->complete_name;
			}
			if( !internal_name ) {
				internal_name = SAFE_MEMBER(object->group_def->property_structs.genesis_properties, pcGenesisCompleteName);
			}
			if( !internal_name ) {
				internal_name = "";
			}

			group = NULL;

			// Create a logical group
			{
				char group_name[ 256 ];
				sprintf( group_name, "LogGrp_%s", object->challenge_name );
				
				for (j = 0; j < eaSize(&root_def->logical_groups); j++)
				{
					if (!strcmp(root_def->logical_groups[j]->group_name, group_name))
					{
						group = root_def->logical_groups[j];
						break;
					}
				}

				if (!group)
				{
					group = StructCreate(parse_LogicalGroup);
					group->group_name = StructAllocString(group_name);
					if (object->challenge_count > 0)
					{
						group->properties = StructCreate(parse_LogicalGroupProperties);
						group->properties->interactableSpawnProperties.eRandomType = LogicalGroupRandomType_OnceOnLoad;
						group->properties->interactableSpawnProperties.eSpawnAmountType = LogicalGroupSpawnAmountType_Number;
						group->properties->interactableSpawnProperties.uSpawnAmount = object->challenge_count;
						group->properties->encounterSpawnProperties.eRandomType = LogicalGroupRandomType_OnceOnLoad;
						group->properties->encounterSpawnProperties.eSpawnAmountType = LogicalGroupSpawnAmountType_Number;
						group->properties->encounterSpawnProperties.uSpawnAmount = object->challenge_count;
					}
					eaPush(&root_def->logical_groups, group);
				}
			}
			//printf("Placing %s\n", external_name);

			genesisSetInternalObjectLogicalName(object, external_name, internal_name, internal_path, root_def, group);
		}

		if (object->trap_name)
		{
			genesisSetInternalObjectLogicalName(object, object->trap_name, "Trap_Core", NULL, root_def, NULL);
		}

		if (object->spawn_name)
		{
			if (spawn_internal_path)
			{
				genesisSetInternalObjectLogicalName(object, object->spawn_name, object->group_def->property_structs.genesis_challenge_properties->spawn_name, spawn_internal_path, root_def, NULL);
			}
			else
			{
				GroupDef *def = object->group_def ? object->group_def : objectLibraryGetGroupDef(object->uid, false);
				if (groupDefScopeIsNameUsed(def, "SpawnPoint"))
					genesisSetInternalObjectLogicalName(object, object->spawn_name, "SpawnPoint", NULL, root_def, NULL);
				else
				{
					// Create a spawn point here
					GenesisToPlaceObject *spawn_object = calloc(1, sizeof(GenesisToPlaceObject));
					GroupDef *spawn_def = objectLibraryGetGroupDefByName("Goto Spawn Point", false);
					spawn_object->uid = spawn_def->name_uid;
					copyMat4(object->mat, spawn_object->mat);
					spawn_object->mat_relative = object->mat_relative;
					spawn_object->challenge_name = strdup(object->spawn_name);
					spawn_object->parent = object->parent;
					eaPush(&to_place->objects, spawn_object);
				}
			}
		}
		
		SAFE_FREE(external_name);
	}

	////////////////////////////////////////////////////////
	/// 3. Clean up all placement state
	eaDestroyEx(&to_place->patrols, genesisDestroyToPlacePatrol);
	eaDestroyEx(&to_place->objects, genesisDestroyToPlaceObject);
	eaDestroyEx(&to_place->platform_groups, genesisDestroyToPlacePlatformGroup);

	if(random_table)
		mersenneTableFree(random_table);
}

//////////////////////////////////////////////////////////////////
// Procedural Map actions
//////////////////////////////////////////////////////////////////

void genesisRebuildLayers(int iPartitionIdx, ZoneMap *zmap, bool external_map)
{
	int i;

	genesisResetLayout();

	if (zmap->map_info.genesis_data)
	{
		if (wlIsServer() || external_map)
			wl_state.genesis_generate_missions_func(&zmap->map_info);
		
		wl_state.genesis_generate_func(iPartitionIdx, zmap, true, false);
	}

	if( !external_map ) {
		for (i = 0; i < eaSize(&zmap->layers); ++i)
		{
			layerReload(zmap->layers[i], true);
			zmap->layers[i]->target_mode = zmap->layers[i]->layer_mode;
		}
	}
}

//////////////////////////////////////////////////////////////////
// MapDescription Library
//////////////////////////////////////////////////////////////////

static int genesisMapDescResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GenesisMapDescription *pMapDesc, U32 userID)
{
	switch (eType)
	{
	xcase RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename((char**)&pMapDesc->filename, "genesis/mapdescriptions", pMapDesc->scope, pMapDesc->name, "mapdesc");
		return VALIDATE_HANDLED;
	xcase RESVALIDATE_CHECK_REFERENCES:
		genesisMapDescriptionValidate(pMapDesc);		
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


int genesisResourceLoad(void)
{
	if (IsServer() && !isProductionMode())
	{
		resLoadResourcesFromDisk(g_MapDescDictionary, "genesis/mapdescriptions", ".mapdesc", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
		resLoadResourcesFromDisk(g_EpisodeDictionary, "genesis/episodes", ".episode", NULL, RESOURCELOAD_SHAREDMEMORY | PARSER_OPTIONALFLAG);
	}
	return 1;
}

/// Episode Library
static int genesisEpisodeResValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GenesisEpisode *pEpisode, U32 userID)
{
	switch (eType)
	{
	xcase RESVALIDATE_FIX_FILENAME:
		resFixPooledFilename((char**)&pEpisode->filename, "genesis/episodes", pEpisode->scope, pEpisode->name, "episode");
		return VALIDATE_HANDLED;
	//xcase RESVALIDATE_POST_TEXT_READING:
		// TODO: Validate episode
	}
	return VALIDATE_NOT_HANDLED;
}

void genesisSetWLGenerateFunc(GenesisGenerateFunc func, GenesisGenerateMissionsFunc missionFunc, GenesisGenerateEpisodeMissionFunc episodeMissionFunc, GenesisGetSpawnPositionsFunc getSpawnPosFunc)
{
	wl_state.genesis_generate_func = func;
	wl_state.genesis_generate_missions_func = missionFunc;
	wl_state.genesis_generate_episode_mission_func = episodeMissionFunc;
	wl_state.genesis_get_spawn_pos_func = getSpawnPosFunc;
}

void genesisReloadLayers(ZoneMap *zmap)
{
	int i;
	for (i = 0; i < eaSize(&zmap->layers); ++i)
	{
		layerReload(zmap->layers[i], true);
		zmap->layers[i]->target_mode = zmap->layers[i]->layer_mode;
	}
}

static bool genesisValidateRefAndTags(char* dictionary, GenesisTagOrName type, void *ref_data, const char *ref_string, 
									  const char **tag_list, const char **append_tag_list,
									  const char *file_name, const char *parent_type, const char *child_type)
{
	bool ret = true;

	PERFINFO_AUTO_START_FUNC();

	if(type == GenesisTagOrName_SpecificByName)
	{
		if(!ref_data && ref_string)
		{
			ErrorFilenamef(file_name, "%s refrences %s that does not exist.  Name: (%s)", parent_type, child_type, ref_string);
			ret = false;
		} 
	}
	else if (type == GenesisTagOrName_RandomByTag)
	{
		if(eaSize(&tag_list) > 0 || eaSize(&append_tag_list) > 0)
		{
			ResourceSearchResult* results = NULL;
			results = genesisDictionaryItemsFromTagList(dictionary, tag_list, append_tag_list, true );
			if(eaSize(&results->eaRows) == 0)
			{
				ErrorFilenamef(file_name, "%s has tags for %s, but no %s has those tags.", parent_type, child_type, child_type);
				ret = false;			
			}
			StructDestroy( parse_ResourceSearchResult, results );		
		}
	}
	PERFINFO_AUTO_STOP();
	return ret;
}

static bool genesisMapDescDetailKitValidate(GenesisDetailKitLayout *detail_kit, const char *file_name, const char *layout_type)
{
	bool ret = true;

	if(!detail_kit)
		return ret;

	if(!genesisValidateRefAndTags(	GENESIS_DETAIL_DICTIONARY, detail_kit->detail_kit_specifier,
									GET_REF(detail_kit->detail_kit), 
									REF_STRING_FROM_HANDLE(detail_kit->detail_kit), 
									detail_kit->detail_tag_list, NULL, file_name, layout_type, "Detail Kit"))
	{
		ret = false;
	}

	return ret;
}

static bool genesisMapDescRoomDetailKitValidate(GenesisRoomDetailKitLayout *room_detail_kit, const char *room_name, const char *file_name)
{
	bool ret = true;

	if(room_detail_kit->detail_specifier != GenesisTagNameDefault_UseDefault)
	{
		GenesisTagOrName specifier = 0;

		if(room_detail_kit->detail_specifier == GenesisTagNameDefault_RandomByTag)
			specifier = GenesisTagOrName_RandomByTag;
		else if (room_detail_kit->detail_specifier == GenesisTagNameDefault_SpecificByName)
			specifier = GenesisTagOrName_SpecificByName;

		if(!genesisValidateRefAndTags(	GENESIS_DETAIL_DICTIONARY, specifier, GET_REF(room_detail_kit->detail_kit), 
										REF_STRING_FROM_HANDLE(room_detail_kit->detail_kit), 
										room_detail_kit->detail_tag_list, NULL, file_name, room_name, "Detail Kit"))
		{
			ret = false;
		}
	}

	return ret;
}

static bool genesisMapDescRoomValidate(GenesisLayoutRoom *room, const char *file_name)
{
	bool ret = true;

	//Room Defs
	if(!genesisValidateRefAndTags(	GENESIS_ROOM_DEF_DICTIONARY, room->room_specifier,
									GET_REF(room->room), REF_STRING_FROM_HANDLE(room->room), 
									room->room_tag_list, NULL, file_name, room->name, "Room Def"))
	{
		ret = false;
	}

	//Detail Kits
	if(!genesisMapDescRoomDetailKitValidate(&room->detail_kit_1, room->name, file_name))
		ret = false;
	if(!genesisMapDescRoomDetailKitValidate(&room->detail_kit_2, room->name, file_name))
		ret = false;

	return ret;
}

static bool genesisMapDescPathValidate(GenesisLayoutPath *path, const char *file_name)
{
	bool ret = true;

	//Path Defs
	if(!genesisValidateRefAndTags(	GENESIS_PATH_DEF_DICTIONARY, path->path_specifier,
									GET_REF(path->path), REF_STRING_FROM_HANDLE(path->path), 
									path->path_tag_list, NULL, file_name, path->name, "Path Def"))
	{
		ret = false;
	}

	//Detail Kits
	if(!genesisMapDescRoomDetailKitValidate(&path->detail_kit_1, path->name, file_name))
		ret = false;
	if(!genesisMapDescRoomDetailKitValidate(&path->detail_kit_2, path->name, file_name))
		ret = false;

	return ret;
}

static bool genesisMapDescSSLibObjValidate(SSLibObj *lib_obj, const char *file_name, const char *parent_type, const char *parent_name)
{
	GroupDef *def = objectLibraryGetGroupDefFromRef(&lib_obj->obj, false);
	if(!def)
	{
		ErrorFilenamef(file_name, "%s (%s) references an object that does not exist.  Name (%s) UID (%d)", parent_type, parent_name, lib_obj->obj.name_str, lib_obj->obj.name_uid);
		return false;
	}
	return true;
}

static bool genesisMapDescSSTagObjValidate(SSTagObj *tag_obj, const char *file_name, const char *parent_type, const char *parent_name)
{
	bool ret = true;
	ResourceSearchResult* results = NULL;
	results = genesisDictionaryItemsFromTagList(OBJECT_LIBRARY_DICT, tag_obj->tags, NULL, true);
	if(eaSize(&results->eaRows) == 0)
	{
		ErrorFilenamef(file_name, "%s (%s) has tags but no object has those tags.", parent_type, parent_name);
		ret = false;			
	}
	StructDestroy( parse_ResourceSearchResult, results );		
	return ret;
}

static bool genesisMapDescPointListValidate(ShoeboxPointList *point_list, const char *file_name)
{
	int i;
	bool ret = true;

	for ( i=0; i < eaSize(&point_list->curve_objects); i++ )
	{
		if(!genesisMapDescSSLibObjValidate(point_list->curve_objects[i], file_name, "Point List", point_list->name))
			ret = false;
	}

	for ( i=0; i < eaSize(&point_list->curve_objects_tags); i++ )
	{
		if(!genesisMapDescSSTagObjValidate(point_list->curve_objects_tags[i], file_name, "Point List", point_list->name))
			ret = false;
	}

	if(point_list->orbit_object)
	{
		for ( i=0; i < eaSize(&point_list->orbit_object->group_refs); i++ )
		{
			if(!genesisMapDescSSLibObjValidate(point_list->orbit_object->group_refs[i], file_name, "Point List", point_list->name))
				ret = false;
		}

		for ( i=0; i < eaSize(&point_list->orbit_object->object_tags); i++ )
		{
			if(!genesisMapDescSSTagObjValidate(point_list->orbit_object->object_tags[i], file_name, "Point List", point_list->name))
				ret = false;
		}	
	}

	return ret;
}

static bool genesisMapDescShoeboxValidate(GenesisShoeboxLayout *shoebox, const char *file_name, const char *layout_name)
{
	int i;
	bool ret = true;
	
	for ( i=0; i < eaSize(&shoebox->detail_objects); i++ )
	{
		if(!genesisMapDescSSLibObjValidate(shoebox->detail_objects[i], file_name, "Layout", layout_name))
			ret = false;
	}

	for ( i=0; i < eaSize(&shoebox->detail_objects_tags); i++ )
	{
		if(!genesisMapDescSSTagObjValidate(shoebox->detail_objects_tags[i], file_name, "Layout", layout_name))
			ret = false;
	}

	for ( i=0; i < eaSize(&shoebox->point_lists); i++ )
	{
		if(!genesisMapDescPointListValidate(shoebox->point_lists[i], file_name))
			ret = false;
	}
	
	return ret;
}

static bool genesisMapDescCommonLayoutDataValidate(GenesisLayoutCommonData *common, const char **append_tags, const char *file_name, const char *layout_name)
{
	bool ret = true;

	if(!common)
		return true;

	if(!genesisValidateRefAndTags(	GENESIS_BACKDROP_FILE_DICTIONARY, common->backdrop_info.backdrop_specifier,
		GET_REF(common->backdrop_info.backdrop), REF_STRING_FROM_HANDLE(common->backdrop_info.backdrop), 
		common->backdrop_info.backdrop_tag_list, append_tags, 
		file_name, layout_name, "Backdrop"))
	{
		ret = false;
	}

	return ret;
}

static bool genesisMapDescSolarSystemValidate(GenesisSolSysLayout *layout, const char *file_name)
{
	bool ret = true;

	if(!layout)
		return true;

	if(!genesisMapDescCommonLayoutDataValidate(&layout->common_data, layout->environment_tags, file_name, layout->name))
		ret = false;

	if(!genesisMapDescShoeboxValidate(&layout->shoebox, file_name, layout->name))
		ret = false;

	return ret;
}

static bool genesisMapDescExteriorInfoValidate(GenesisExteriorLayoutInfo *layout_info, const char *file_name)
{
	bool ret = true;
	if(!layout_info)
		return ret;

	//Geotype
	if(!genesisValidateRefAndTags(	GENESIS_GEOTYPE_DICTIONARY, layout_info->geotype_specifier,
		GET_REF(layout_info->geotype), REF_STRING_FROM_HANDLE(layout_info->geotype), 
		layout_info->geotype_tag_list, NULL, file_name, "Exterior Layout", "Geotype"))
	{
		ret = false;
	}

	//Ecosystem
	if(!genesisValidateRefAndTags(	GENESIS_ECOTYPE_DICTIONARY, layout_info->ecosystem_specifier,
		GET_REF(layout_info->ecosystem), REF_STRING_FROM_HANDLE(layout_info->ecosystem), 
		layout_info->ecosystem_tag_list, NULL, file_name, "Exterior Layout", "Ecosystem"))
	{
		ret = false;
	}
	return ret;
}

static bool genesisMapDescExteriorValidate(GenesisExteriorLayout *layout, const char *file_name)
{
	int i;
	bool ret = true;

	if(!layout)
		return ret;

	if(!genesisMapDescCommonLayoutDataValidate(&layout->common_data, NULL, file_name, layout->name))
		ret = false;

	//Layout Info
	if(!genesisMapDescExteriorInfoValidate(&layout->info, file_name))
		ret = false;

	//Detail Kits
	if(!genesisMapDescDetailKitValidate(&layout->detail_kit_1, file_name, "Exterior Layout"))
		ret = false;
	if(!genesisMapDescDetailKitValidate(&layout->detail_kit_2, file_name, "Exterior Layout"))
		ret = false;

	//Rooms
	for ( i=0; i < eaSize(&layout->rooms); i++ )
	{
		if(!genesisMapDescRoomValidate(layout->rooms[i], file_name))
			ret = false;
	}

	//Paths
	for ( i=0; i < eaSize(&layout->paths); i++ )
	{
		if(!genesisMapDescPathValidate(layout->paths[i], file_name))
			ret = false;
	}

	return ret;
}

static bool genesisMapDescInteriorInfoValidate(GenesisInteriorLayoutInfo *layout_info, const char *file_name)
{
	bool ret = true;

	if(!layout_info)
		return ret;

	//Interior Kit
	if(!genesisValidateRefAndTags(	GENESIS_INTERIORS_DICTIONARY, layout_info->room_kit_specifier,
		GET_REF(layout_info->room_kit), REF_STRING_FROM_HANDLE(layout_info->room_kit), 
		layout_info->room_kit_tag_list, NULL, file_name, "Interior Layout", "Interior Kit"))
	{
		ret = false;
	}

	//Light Kit
	if(!genesisValidateRefAndTags(	GENESIS_INTERIORS_DICTIONARY, layout_info->light_kit_specifier,
		GET_REF(layout_info->light_kit), REF_STRING_FROM_HANDLE(layout_info->light_kit), 
		layout_info->light_kit_tag_list, NULL, file_name, "Interior Layout", "Light Kit"))
	{
		ret = false;
	}

	return ret;
}

static bool genesisMapDescInteriorValidate(GenesisInteriorLayout *layout, const char *file_name)
{
	int i;
	bool ret = true;

	if(!layout)
		return ret;

	if(!genesisMapDescCommonLayoutDataValidate(&layout->common_data, NULL, file_name, layout->name))
		ret = false;

	if(!genesisMapDescInteriorInfoValidate(&layout->info, file_name))
		ret = false;

	//Detail Kits
	if(!genesisMapDescDetailKitValidate(&layout->detail_kit_1, file_name, "Interior Layout"))
		ret = false;
	if(!genesisMapDescDetailKitValidate(&layout->detail_kit_2, file_name, "Interior Layout"))
		ret = false;

	//Rooms
	for ( i=0; i < eaSize(&layout->rooms); i++ )
	{
		if(!genesisMapDescRoomValidate(layout->rooms[i], file_name))
			ret = false;
	}

	//Paths
	for ( i=0; i < eaSize(&layout->paths); i++ )
	{
		if(!genesisMapDescPathValidate(layout->paths[i], file_name))
			ret = false;
	}

	return ret;
}

static bool genesisMapDescriptionValidate(GenesisMapDescription *map_desc)
{
	int i;
	bool ret = true;

	PERFINFO_AUTO_START_FUNC();

	for ( i=0; i < eaSize(&map_desc->solar_system_layouts); i++ )
	{
		GenesisSolSysLayout *layout = map_desc->solar_system_layouts[i];
		genesisMapDescSolarSystemValidate(layout, map_desc->filename);
	}
	if(map_desc->exterior_layout)
	{
		GenesisExteriorLayout *layout = map_desc->exterior_layout;
		genesisMapDescExteriorValidate(layout, map_desc->filename);
		if(layout->layout_info_specifier == GenesisTemplateOrCustom_Template)
		{
			if(!genesisValidateRefAndTags(	GENESIS_EXT_LAYOUT_TEMP_FILE_DICTIONARY, GenesisTagOrName_SpecificByName,
				GET_REF(layout->ext_template), REF_STRING_FROM_HANDLE(layout->ext_template), 
				NULL, NULL, map_desc->filename, "Map Description", "Exterior Layout Template"))
			{
				ret = false;
			}
		}
	}
	for ( i=0; i < eaSize(&map_desc->interior_layouts); i++ )
	{
		GenesisInteriorLayout *layout = map_desc->interior_layouts[i];
		genesisMapDescInteriorValidate(layout, map_desc->filename);
		if(layout->layout_info_specifier == GenesisTemplateOrCustom_Template)
		{
			if(!genesisValidateRefAndTags(	GENESIS_INT_LAYOUT_TEMP_FILE_DICTIONARY, GenesisTagOrName_SpecificByName,
				GET_REF(layout->int_template), REF_STRING_FROM_HANDLE(layout->int_template), 
				NULL, NULL, map_desc->filename, "Map Description", "Interior Layout Template"))
			{
				ret = false;
			}
		}
	}

	PERFINFO_AUTO_STOP();

	return ret;
}

static bool genesisExteriorLayoutTemplateValidate(GenesisMapDescExteriorLayoutTemplate *ext_template)
{
	bool ret = true;
	char **null_tag_list = NULL;

	//Backdrop
	if(!genesisValidateRefAndTags(	GENESIS_BACKDROP_FILE_DICTIONARY, ext_template->backdrop_info->backdrop_specifier,
		GET_REF(ext_template->backdrop_info->backdrop), REF_STRING_FROM_HANDLE(ext_template->backdrop_info->backdrop), 
		ext_template->backdrop_info->backdrop_tag_list, null_tag_list, 
		ext_template->filename, "Exterior Layout Template", "Backdrop"))
	{
		ret = false;
	}

	//Layout Info
	if(!genesisMapDescExteriorInfoValidate(ext_template->layout_info, ext_template->filename))
		ret = false;

	//Detail Kits
	if(!genesisMapDescDetailKitValidate(ext_template->detail_kit_1, ext_template->filename, "Exterior Layout"))
		ret = false;
	if(!genesisMapDescDetailKitValidate(ext_template->detail_kit_2, ext_template->filename, "Exterior Layout"))
		ret = false;

	return ret;
}

static int genesisExteriorLayoutTemplateValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GenesisMapDescExteriorLayoutTemplate *ext_template, U32 userID)
{
	switch(eType)
	{
	case RESVALIDATE_CHECK_REFERENCES:
		genesisExteriorLayoutTemplateValidate(ext_template);		
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}

static bool genesisInteriorLayoutTemplateValidate(GenesisMapDescInteriorLayoutTemplate *int_template)
{
	bool ret = true;
	char **null_tag_list = NULL;

	//Backdrop
	if(!genesisValidateRefAndTags(	GENESIS_BACKDROP_FILE_DICTIONARY, int_template->backdrop_info->backdrop_specifier,
		GET_REF(int_template->backdrop_info->backdrop), REF_STRING_FROM_HANDLE(int_template->backdrop_info->backdrop), 
		int_template->backdrop_info->backdrop_tag_list, null_tag_list, 
		int_template->filename, "Interior Layout Template", "Backdrop"))
	{
		ret = false;
	}

	//Layout Info
	if(!genesisMapDescInteriorInfoValidate(int_template->layout_info, int_template->filename))
		ret = false;

	//Detail Kits
	if(!genesisMapDescDetailKitValidate(int_template->detail_kit_1, int_template->filename, "Interior Layout"))
		ret = false;
	if(!genesisMapDescDetailKitValidate(int_template->detail_kit_2, int_template->filename, "Interior Layout"))
		ret = false;

	return ret;
}

static int genesisInteriorLayoutTemplateValidateCB(enumResourceValidateType eType, const char *pDictName, const char *pResourceName, GenesisMapDescInteriorLayoutTemplate *int_template, U32 userID)
{
	switch(eType)
	{
	case RESVALIDATE_CHECK_REFERENCES:
		genesisInteriorLayoutTemplateValidate(int_template);		
		return VALIDATE_HANDLED;
	}
	return VALIDATE_NOT_HANDLED;
}


AUTO_RUN;
void genesisInit(void)
{
	g_MapDescDictionary = RefSystem_RegisterSelfDefiningDictionary( GENESIS_MAPDESC_DICTIONARY, false, parse_GenesisMapDescription, true, false, NULL );
	resDictManageValidation(g_MapDescDictionary, genesisMapDescResValidateCB);

	g_EpisodeDictionary = RefSystem_RegisterSelfDefiningDictionary( "Episode", false, parse_GenesisEpisode, true, false, NULL );
	resDictManageValidation(g_EpisodeDictionary, genesisEpisodeResValidateCB);

	if (IsServer())
	{
		resDictProvideMissingResources(g_MapDescDictionary);
		resDictProvideMissingResources(g_EpisodeDictionary);
		if (isDevelopmentMode() || isProductionEditMode()) {
			resDictMaintainInfoIndex(g_MapDescDictionary, ".name", ".scope", NULL, NULL, NULL);
			resDictMaintainInfoIndex(g_EpisodeDictionary, ".name", ".scope", NULL, NULL, NULL);
		}
	} 
	else if (IsClient())
	{
		resDictRequestMissingResources(g_MapDescDictionary, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
		resDictRequestMissingResources(g_EpisodeDictionary, RES_DICT_KEEP_ALL, false, resClientRequestSendReferentCommand);
	}
	resDictProvideMissingRequiresEditMode(g_MapDescDictionary);
	resDictProvideMissingRequiresEditMode(g_EpisodeDictionary);
}

AUTO_COMMAND;
void genesisForceAllowUnfreeze( void )
{
	gGenesisUnfreezeDisabled = false;
}

static bool eaEqual(void*** ea1, void*** ea2, int (*cmpFn)(void* elem1, void* elem2))
{
	if( eaSize(ea1) != eaSize(ea2)) {
		return false;
	} else {
		int it;
		for( it = eaSize(ea1) - 1; it >= 0; --it ) {
			void* elem1 = (*ea1)[it];
			void* elem2 = (*ea2)[it];

			if( cmpFn(elem1, elem2) != 0 ) {
				return false;
			}
		}

		return true;
	}
}

bool genesisTerrainCanSkipUpdate(GenesisMapDescription *old_data, GenesisMapDescription *new_data)
{
	int i, j, k;
	ZoneMapLayer *terrain_layer = zmapGetLayer(NULL, 0);
	
	if(!layerGetDef(terrain_layer))
		return false;

	// Only care about exterior -> exterior changes.
	if (!old_data->exterior_layout || !new_data->exterior_layout)
		return false;

	// Backdrop
	if (old_data->exterior_layout->common_data.backdrop_info.backdrop_specifier != new_data->exterior_layout->common_data.backdrop_info.backdrop_specifier)
		return false;

	switch (old_data->exterior_layout->common_data.backdrop_info.backdrop_specifier)
	{
		case GenesisTagOrName_RandomByTag:
			if (!eaEqual(&old_data->exterior_layout->common_data.backdrop_info.backdrop_tag_list, &new_data->exterior_layout->common_data.backdrop_info.backdrop_tag_list, utils_stricmp))
				return false;
			break;
		case GenesisTagOrName_SpecificByName:
			if (GET_REF(old_data->exterior_layout->common_data.backdrop_info.backdrop) != GET_REF(new_data->exterior_layout->common_data.backdrop_info.backdrop))
				return false;
			break;
		default:
			return false;
	};

	// If the layout is different, regenerate terrain
	if (StructCompare(parse_GenesisExteriorLayout, old_data->exterior_layout, new_data->exterior_layout, 0, 0, 0) != 0)
		return false;

	// Missions
	if (eaSize(&old_data->missions) != eaSize(&new_data->missions))
		return false;

	for (i = 0; i < eaSize(&old_data->missions); i++)
	{
		GenesisMissionDescription *old_mission = old_data->missions[i];
		GenesisMissionDescription *new_mission = new_data->missions[i];
		GenesisMissionStartDescription *old_start = &old_mission->zoneDesc.startDescription;
		GenesisMissionStartDescription *new_start = &new_mission->zoneDesc.startDescription;

		// Entry/exit doors
		if (old_start->bHasEntryDoor != new_start->bHasEntryDoor)
			return false;
		if (old_start->eExitFrom != new_start->eExitFrom)
			return false;
		if (utils_stricmp(old_start->pcStartRoom, new_start->pcStartRoom) != 0)
			return false;
		if (utils_stricmp(old_start->pcExitRoom, new_start->pcExitRoom) != 0)
			return false;

		// Challenges
		if (eaSize(&old_mission->eaChallenges) != eaSize(&new_mission->eaChallenges))
			return false;

		for (j = 0; j < eaSize(&old_mission->eaChallenges); j++)
		{
			GenesisMissionChallenge *old_challenge = old_mission->eaChallenges[j];
			GenesisMissionChallenge *new_challenge = new_mission->eaChallenges[j];

			if (eaSize(&old_challenge->eaRoomNames) != eaSize(&new_challenge->eaRoomNames))
				return false;

			for (k = 0; k < eaSize(&old_challenge->eaRoomNames); k++)
			{
				if (utils_stricmp(old_challenge->eaRoomNames[k], new_challenge->eaRoomNames[k]))
					return false;
			}

			if (old_challenge->bHeterogenousObjects != new_challenge->bHeterogenousObjects)
				return false;

			if (old_challenge->iCount != new_challenge->iCount)
				return false;

			if (old_challenge->ePlacement != new_challenge->ePlacement)
				return false;

			if (old_challenge->eFacing != new_challenge->eFacing)
				return false;

			if (utils_stricmp(old_challenge->pcRefChallengeName, new_challenge->pcRefChallengeName) != 0)
				return false;

			if (SAFE_MEMBER(old_challenge->pEncounter, ePatrolType) != SAFE_MEMBER(new_challenge->pEncounter, ePatrolType))
				return false;

			if (SAFE_MEMBER(old_challenge->pEncounter, ePatPlacement) != SAFE_MEMBER(new_challenge->pEncounter, ePatPlacement))
				return false;

			if (utils_stricmp(SAFE_MEMBER(old_challenge->pEncounter, pcPatRefChallengeName), SAFE_MEMBER(new_challenge->pEncounter, pcPatRefChallengeName)) != 0)
				return false;

			switch (old_challenge->eSpecifier)
			{
				case GenesisTagOrName_RandomByTag:
					if (!eaEqual(&old_challenge->eaChallengeTags, &new_challenge->eaChallengeTags, utils_stricmp))
						return false;
					break;
				case GenesisTagOrName_SpecificByName:
					if (utils_stricmp(old_challenge->pcChallengeName, new_challenge->pcChallengeName))
						return false;
					break;
				default:
					return false;
			};
		}
	}

	return true;
}

GenesisProceduralObjectParams* genesisCreateStartSpawn( const char* transition )
{
	GenesisProceduralObjectParams* params = StructCreate( parse_GenesisProceduralObjectParams );
	params->spawn_properties = StructCreate( parse_WorldSpawnProperties );
	params->spawn_properties->spawn_type = SPAWNPOINT_STARTSPAWN;
	SET_HANDLE_FROM_STRING("DoorTransitionSequenceDef", transition, params->spawn_properties->hTransSequence);
	return params;
}

GenesisProceduralObjectParams* genesisCreateMultiMissionWrapperParams(void)
{
	GenesisProceduralObjectParams* params = StructCreate(parse_GenesisProceduralObjectParams);
	WorldInteractionPropertyEntry* entry = StructCreate( parse_WorldInteractionPropertyEntry );
	
	params->interaction_properties = StructCreate(parse_WorldInteractionProperties);
	params->interaction_properties->pChildProperties = StructCreate(parse_WorldChildInteractionProperties);
	params->interaction_properties->pChildProperties->pChildSelectExpr = exprCreateFromString("GetMapVariableInt(\"Mission_Num\")", NULL);
	eaPush(&params->interaction_properties->eaEntries, entry);
	entry->pcInteractionClass = allocAddString( "NAMEDOBJECT" );

	return params;
}

/// Find and return a MissionDescription named MISSION-NAME in MAP-DESC.
int genesisFindMission( GenesisMapDescription* mapDesc, const char* missionName )
{
	int it;

	if( mapDesc ) {
		for( it = 0; it != eaSize( &mapDesc->missions ); ++it ) {
			if( stricmp( mapDesc->missions[ it ]->zoneDesc.pcName, missionName ) == 0 ) {
				return it;
			}
		}
	}

	return -1;
}

void genesisSetErrorOnEncounter1( bool state )
{
	wl_state.genesis_error_on_encounter1 = state;
}

/// Return true if this map is a StarCluster map.
///
/// TODO: make this data driven
bool genesisIsStarClusterMap(ZoneMapInfo* zminfo)
{
	return (stricmp( GetShortProductName(), "ST" ) == 0 && zmapInfoGetFilename( zminfo )
			&& strstr( zmapInfoGetFilename( zminfo ), "/Star_Cluster/" ));
}

void genesisWorldCellSetEditable(void)
{
	worldCellSetEditable();
}

#endif

/// Get the genesis config data
GenesisConfig* genesisConfig(void)
{
	static GenesisConfig* config = NULL;

	if( !config ) {
		config = StructCreate( parse_GenesisConfig );
		ParserLoadFiles(NULL, "defs/config/GenesisConfig.def", "GenesisConfig.bin", PARSER_BINS_ARE_SHARED, parse_GenesisConfig, config );
	}

	return config;
}

GenesisConfigCheckedAttrib* genesisCheckedAttrib( const char* attribName )
{
	GenesisConfig* config = genesisConfig();
	int it;

	attribName = allocFindString( attribName );
	for( it = 0; it != eaSize( &config->checkedAttribs ); ++it ) {
		GenesisConfigCheckedAttrib* attrib = config->checkedAttribs[ it ];
		if( attrib->name == attribName ) {
			return attrib;
		}
	}

	return NULL;
}

static const char* genesisGetLayerNameFromType(GenesisMapType type, int layer_idx)
{
	static char layer_name[256];
	switch(type)
	{
	case GenesisMapType_Exterior:
		sprintf(layer_name, "GenesisExterior_%d", layer_idx+1);
		break;
	case GenesisMapType_Interior:
		sprintf(layer_name, "GenesisInterior_%d", layer_idx+1);
		break;
	case GenesisMapType_SolarSystem:
		sprintf(layer_name, "GenesisSolarSystem_%d", layer_idx+1);
		break;
	case GenesisMapType_MiniSolarSystem:
		sprintf(layer_name, "GenesisMiniSolSys_%d", layer_idx+1);
		break;
	case GenesisMapType_UGC_Space:
		sprintf(layer_name, "GenesisUGCSpace_%d", layer_idx+1);
		break;
	case GenesisMapType_UGC_Prefab:
		sprintf(layer_name, "GenesisUGCPrefab_%d", layer_idx+1);
		break;
	default:
		assert(false);
	}
	return layer_name;
}


ResourceSearchResult* genesisDictionaryItemsFromTag( char* dictionary, char* tags, bool validation_only )
{
	ResourceSearchRequest request = {0};
	PERFINFO_AUTO_START_FUNC();
	request.eSearchMode = SEARCH_MODE_TAG_SEARCH;
	request.one_result = validation_only;
	request.pcSearchDetails = tags;

	request.pcName = NULL;
	request.pcType = dictionary;
	request.iRequest = 1;
	{
		ResourceSearchResult* result = handleResourceSearchRequest( &request );
		PERFINFO_AUTO_STOP();
		return result;
	}
}

ResourceSearchResult* genesisDictionaryItemsFromTagList( char* dictionary, const char** tag_list, const char** append_tag_list, bool validation_only )
{
	char* estr = NULL;
	PERFINFO_AUTO_START_FUNC();
	
	{
		int it;
		for( it = 0; it != eaSize( &tag_list ); ++it ) {
			estrConcatf( &estr, "%s, ", tag_list[ it ]);
		}
		for( it = 0; it != eaSize( &append_tag_list ); ++it ) {
			estrConcatf( &estr, "%s, ", append_tag_list[ it ]);
		}
	}

	if( estr ) {
		estrSetSize( &estr, estrLength( &estr ) - 2 );
	}

	{
		ResourceSearchResult* result = genesisDictionaryItemsFromTag(dictionary, estr, validation_only );
		estrDestroy( &estr );
		PERFINFO_AUTO_STOP();
		return result;
	}
}

void* genesisGetRandomItemFromTag(MersenneTable *table, char *dictionary, char *tags, char* append_tags)
{
	void *ret = NULL;
	int row_cnt;
	ResourceSearchResult *results;
	ResourceSearchRequest request = {0};
	request.eSearchMode = SEARCH_MODE_TAG_SEARCH;
	
	if (append_tags)
	{
		if (tags)
		{
			size_t buf_size = strlen(tags) + 1 + strlen(append_tags) + 1;
			char* buf = alloca(buf_size);
			sprintf_s(SAFESTR2(buf), "%s,%s", tags, append_tags);
			request.pcSearchDetails = buf;
		}
		else
		{
			request.pcSearchDetails = append_tags;
		}
	}
	else
	{
		request.pcSearchDetails = tags;
	}
	
	request.pcName = NULL;
	request.pcType = dictionary;
	request.iRequest = 1;
	results = handleResourceSearchRequest(&request);

	row_cnt = eaSize(&results->eaRows);
	if(row_cnt > 0)
	{
		int rand_idx = randomMersenneU32(table)%row_cnt;
		if(stricmp(dictionary, OBJECT_LIBRARY_DICT)==0)
			ret = objectLibraryGetGroupDefByName(results->eaRows[rand_idx]->pcName, false);
		else
			ret = RefSystem_ReferentFromString(dictionary, results->eaRows[rand_idx]->pcName);
	}
	StructDestroy(parse_ResourceSearchResult, results);
	return ret;
}

static void* genesisGetRandomItemFromTagList(MersenneTable *table, char *dictionary, char **tag_list, char **append_tag_list)
{
	char* estr = NULL;
	
	{
		int it;
		for( it = 0; it != eaSize( &tag_list ); ++it ) {
			estrConcatf( &estr, "%s, ", tag_list[ it ]);
		}
		for( it = 0; it != eaSize( &append_tag_list ); ++it ) {
			estrConcatf( &estr, "%s, ", append_tag_list[ it ]);
		}
	}

	if( estr ) {
		estrSetSize( &estr, estrLength( &estr ) - 2 );
	}

	{
		void* item = genesisGetRandomItemFromTag(table, dictionary, estr, NULL );
		estrDestroy( &estr );
		return item;
	}
}

static void* genesisGetRandomItemFromTagList1(MersenneTable *table, char *dictionary, char **tag_list, char *append_tag)
{
	char* estr = NULL;
	
	{
		int it;
		for( it = 0; it != eaSize( &tag_list ); ++it ) {
			estrConcatf( &estr, "%s, ", tag_list[ it ]);
		}
		if( append_tag ) {
			estrConcatf( &estr, "%s, ", append_tag );
		}
	}

	if( estr ) {
		estrSetSize( &estr, estrLength( &estr ) - 2 );
	}

	{
		void* item = genesisGetRandomItemFromTag(table, dictionary, estr, NULL );
		estrDestroy( &estr );
		return item;
	}
}

ZoneMapLayer *genesisMakeSingleLayer(ZoneMap *zmap, const char *layer_name)
{
	char temp_str[MAX_PATH], *dir_start;
	char layer_filename[MAX_PATH];
	ZoneMapLayer *output_layer;

	strcpy(temp_str, zmapGetFilename(zmap));
	dir_start = (temp_str[0] == '/') ? &temp_str[1] : &temp_str[0];
	sprintf(layer_filename, "%s/%s.layer", getDirectoryName(dir_start), layer_name);
	output_layer = zmapNewLayer(zmap, eaSize(&zmap->layers), layer_filename);
	assert(output_layer);
	output_layer->layer_mode = LAYER_MODE_NOT_LOADED;
	groupLibFree(output_layer->grouptree.def_lib);
	output_layer->grouptree.def_lib = NULL;
	sprintf(layer_filename, "%s.layer", layer_name);
	output_layer->name = strdup(layer_filename);
	output_layer->genesis = true;
	return output_layer;
}

static void genesisFixupZoneMapInfoBackdrop(GenesisZoneMapInfoLayer *layer_info, GenesisBackdrop *backdrop)
{
	if (backdrop)
	{
		layer_info->region_sky_group = StructClone(parse_SkyInfoGroup, backdrop->sky_group);
		layer_info->region_override_cubemap = backdrop->override_cubemap;
	}
}

void genesisFixupZoneMapInfo(ZoneMapInfo *info)
{
	int i;
	GenesisBackdrop *backdrop=NULL;
	GenesisZoneMapData *data = info->genesis_data;

	if (!data)
		return;

	if (info->genesis_info)
		StructDestroy(parse_GenesisZoneMapInfo, info->genesis_info);
	info->genesis_info = StructCreate(parse_GenesisZoneMapInfo);

	for ( i=0; i < eaSize(&data->solar_systems); i++ )
	{
		GenesisZoneMapInfoLayer *layer_info = StructCreate(parse_GenesisZoneMapInfoLayer);
		eaPush(&info->genesis_info->solsys_layers, layer_info);
		genesisFixupZoneMapInfoBackdrop(layer_info, data->solar_systems[i]->backdrop);
	}
	for ( i=0; i < eaSize(&data->genesis_interiors); i++ )
	{
		GenesisZoneMapInfoLayer *layer_info = StructCreate(parse_GenesisZoneMapInfoLayer);
		eaPush(&info->genesis_info->interior_layers, layer_info);
		genesisFixupZoneMapInfoBackdrop(layer_info, data->genesis_interiors[i]->backdrop);
	}

	if (data->genesis_exterior || data->genesis_exterior_nodes)
	{
		GenesisZoneMapInfoLayer *layer_info = StructCreate(parse_GenesisZoneMapInfoLayer);
		eaPush(&info->genesis_info->exterior_layers, layer_info);
		info->genesis_info->is_vista = false;

		if (data->genesis_exterior)
		{
			backdrop = data->genesis_exterior->backdrop;
			info->genesis_info->is_vista = data->genesis_exterior->is_vista;
			info->genesis_info->vista_map = data->genesis_exterior->vista_map;
			genesisFixupZoneMapInfoBackdrop(layer_info, data->genesis_exterior->backdrop);
		}
		else
		{
			backdrop = data->genesis_exterior_nodes->backdrop;
			info->genesis_info->is_vista = data->genesis_exterior_nodes->is_vista;
			info->genesis_info->vista_map = data->genesis_exterior_nodes->vista_map;
			genesisFixupZoneMapInfoBackdrop(layer_info, data->genesis_exterior_nodes->backdrop);
		}
	}

	if (eaSize(&info->genesis_info->interior_layers) == 0 &&
		eaSize(&info->genesis_info->exterior_layers) == 0 &&
		eaSize(&info->genesis_info->solsys_layers) == 0)
	{
		// Create a single error layer
		GenesisZoneMapInfoLayer *layer_info = StructCreate(parse_GenesisZoneMapInfoLayer);
		eaPush(&info->genesis_info->interior_layers, layer_info);
	}

	//info->genesis_info->seed = data->seed;

	// TomY ENCOUNTER_HACK this block
	if (wlIsServer())
	{
		// Mission data
		if (eaSize(&data->genesis_mission) > 0)
		{
			info->genesis_info->mission_level = data->genesis_mission[0]->desc.levelDef.missionLevel;
			info->genesis_info->level_type = data->genesis_mission[0]->desc.levelDef.eLevelType;
		}
		else
		{
			info->genesis_info->mission_level = 0;
			info->genesis_info->level_type = MissionLevelType_Specified;
		}

		eaCopyStructs(&data->encounter_overrides, &info->genesis_info->encounter_overrides, parse_GenesisProceduralEncounterProperties);
	}
}

GenesisLayerBoundsList* genesisMakeBoundsList(GenesisZoneMapInfo *info)
{
	GenesisLayerBoundsList *bounds_list;
	if(!info)
		return NULL;
	bounds_list = StructCreate(parse_GenesisLayerBoundsList);

	//if(info->ext_layer_cnt)

	return bounds_list;
}

static void genesisMakeLayer(ZoneMap *zmap, GenesisZoneMapInfoLayer *layer_info, GenesisMapType map_type, WorldRegionType region_type, int layer_idx)
{
	ZoneMapLayer *layer;
	WorldRegion *region;
	const char *layer_name;
	
	layer_name = genesisGetLayerNameFromType(map_type, layer_idx);
	layer = genesisMakeSingleLayer(zmap, layer_name);

	assert(!layer->region_name);
	// TomY TODO UGC - is this the right thing to do for non-exterior prefabs?
	if(map_type != GenesisMapType_Exterior && map_type != GenesisMapType_UGC_Prefab)
		layer->region_name = allocAddString(layer_name);
	region = layerGetWorldRegion(layer);
	zmapRegionSetType(zmap, region, region_type);

	if (layer_info->region_sky_group)
		region->sky_group = StructClone(parse_SkyInfoGroup, layer_info->region_sky_group);
	region->override_cubemap = layer_info->region_override_cubemap;

	//Solar System mini overview map
	if(map_type == GenesisMapType_SolarSystem || map_type == GenesisMapType_UGC_Prefab || map_type == GenesisMapType_UGC_Space)
	{
		ZoneMapLayer *system_layer;
		DependentWorldRegion *dependant = StructCreate(parse_DependentWorldRegion);

		layer_name = genesisGetLayerNameFromType(GenesisMapType_MiniSolarSystem, layer_idx);
		system_layer = genesisMakeSingleLayer(zmap, layer_name);

		assert(!system_layer->region_name);
		system_layer->region_name = allocAddString(layer_name);
		zmapRegionSetType(zmap, layerGetWorldRegion(system_layer), WRT_SystemMap);

		eaDestroyStruct(&region->dependents, parse_DependentWorldRegion);
		dependant->name = allocAddString(layer_name);
		dependant->hidden_object_id = layer_idx+1;
		eaPush(&region->dependents, dependant);
	}

	//Ensure that there is a default region, and then set it to our type
	//This is really only important until we support multi map types
	if (eaSize(&zmap->map_info.regions) <= 0 || zmap->map_info.regions[0]->name)
		createWorldRegion(zmap, NULL);
	zmapRegionSetType(zmap, zmapGetWorldRegionByName(zmap, NULL), region_type);	
}

void genesisMakeLayers(ZoneMap *zmap)
{
	int i;
	GenesisZoneMapInfo *info = zmap->map_info.genesis_info;

	zmap->map_info.no_beacons = 0;
	zmap->map_info.not_player_visited = 0;

	for ( i=0; i < eaSize(&info->solsys_layers); i++ )
		genesisMakeLayer(zmap, info->solsys_layers[i], GenesisMapType_SolarSystem, WRT_Space, i);
	for ( i=0; i < eaSize(&info->interior_layers); i++ )
		genesisMakeLayer(zmap, info->interior_layers[i], GenesisMapType_Interior, WRT_Ground, i);
	for ( i=0; i < eaSize(&info->exterior_layers); i++ )
		genesisMakeLayer(zmap, info->exterior_layers[i], GenesisMapType_Exterior, WRT_Ground, i);

	eaDestroyStruct(&zmap->map_info.secondary_maps, parse_SecondaryZoneMap);
	if (info->vista_map)
	{
		SecondaryZoneMap *zmap_ref;
		GenesisConfig* config = genesisConfig();
		int vista_hole_size = SAFE_MEMBER(config, vista_hole_size);
		if(vista_hole_size <= 0)
			vista_hole_size = GENESIS_EXTERIOR_DEFAULT_VISTA_HOLE_SIZE;

		zmap_ref = StructCreate(parse_SecondaryZoneMap);
		zmap_ref->map_name = StructAllocString(info->vista_map);
		eaPush(&zmap->map_info.secondary_maps, zmap_ref);

		zmap->map_info.no_beacons = 1;
		zmap->map_info.not_player_visited = 1;

		setVec2(zmap->map_info.terrain_playable_min, 0, 0);
		setVec2(zmap->map_info.terrain_playable_max, vista_hole_size*CELL_BLOCK_SIZE, vista_hole_size*CELL_BLOCK_SIZE);
	}
	else if (info->external_map)
	{
		SecondaryZoneMap *zmap_ref;
		GenesisConfig* config = genesisConfig();

		zmap_ref = StructCreate(parse_SecondaryZoneMap);
		zmap_ref->map_name = StructAllocString(info->external_map);
		eaPush(&zmap->map_info.secondary_maps, zmap_ref);
	}

	zmapFixupLayerFilenames(zmap);
}

bool genesisDataHasTerrain(GenesisZoneMapData *data)
{
	return (data->genesis_exterior != NULL || data->genesis_exterior_nodes != NULL);
}

void genesisResetLayout()
{
	if (wl_state.genesis_node_layout)
		StructDestroy(parse_GenesisZoneNodeLayout, wl_state.genesis_node_layout);
	wl_state.genesis_node_layout = NULL;
}

U32 genesisGetBinVersion(ZoneMap *zmap)
{
	int ext_cnt=0, int_cnt=0, sol_cnt=0;
	int ret = 0;
	if (zmap->map_info.genesis_info) {
		ext_cnt = eaSize(&zmap->map_info.genesis_info->exterior_layers);
		int_cnt = eaSize(&zmap->map_info.genesis_info->interior_layers);
		sol_cnt = eaSize(&zmap->map_info.genesis_info->solsys_layers);
	} else if (zmap->map_info.genesis_data) {
		ext_cnt = (zmap->map_info.genesis_data->genesis_exterior ? 1 : 0);
		int_cnt = eaSize(&zmap->map_info.genesis_data->genesis_interiors);
		sol_cnt = eaSize(&zmap->map_info.genesis_data->solar_systems);
	}

	if(ext_cnt != 0)
		ret += GENESIS_EXTERIOR_VERSION*1000;
	if(int_cnt != 0)
		ret += GENESIS_INTERIOR_VERSION *100000;
	if(sol_cnt != 0)
		ret += SOLAR_SYSTEM_VERSION *10000000;
	return ret;
}

static void genesisRaiseErrorV(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, char* message, va_list ap )
{
#ifdef NO_EDITORS
	{
		char msg_buffer[1024];
		vsprintf(msg_buffer, message, ap);

		if (type == GENESIS_WARNING)
		{
			Alertf("%s", msg_buffer);
		}
		else
		{
			ErrorFilenamef(zmapGetFilename(NULL), "%s", msg_buffer);
		}
	}
#else
	{
		char msg_buffer[1024];
		vsprintf(msg_buffer, message, ap);
		if (gCurrentStage)
		{
			GenesisRuntimeError *error = StructCreate(parse_GenesisRuntimeError);
			error->type = type;
			error->context = StructClone( parse_GenesisRuntimeErrorContext, context );
			error->message = StructAllocString(msg_buffer);
			eaPush(&gCurrentStage->errors, error);
		}
		else
		{
			if (wlIsServer() && zmapLocked(NULL))
				return;
			if (type == GENESIS_WARNING)
			{
				Alertf("%s", msg_buffer);
			}
			else
			{
				ErrorFilenamef(zmapGetFilename(NULL), "%s", msg_buffer);
			}
		}
	}
#endif
}

static void genesisRaiseUGCErrorV(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, const char* fieldName, const char *message_key, const char* extraText, char* message, va_list ap )
{
	char msg_buffer[1024];
	vsprintf(msg_buffer, message, ap);
	if (gCurrentStage)
	{
		GenesisRuntimeError *error = StructCreate(parse_GenesisRuntimeError);
		error->type = type;
		error->context = StructClone( parse_GenesisRuntimeErrorContext, context );
		error->field_name = StructAllocString(fieldName);
		error->message = StructAllocString(msg_buffer);
		error->message_key = allocAddString(message_key);
		error->extraText = StructAllocString( extraText );
		eaPush(&gCurrentStage->errors, error);
	}
	else
	{
		if (wlIsServer() && zmapLocked(NULL))
			return;
		if (type == GENESIS_WARNING)
		{
			Alertf("%s", msg_buffer);
		}
		else
		{
			ErrorFilenamef(zmapGetFilename(NULL), "%s", msg_buffer);
		}
	}
}

void genesisRaiseError(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, char* message, ...)
{
	va_list ap;
	va_start( ap, message );
	genesisRaiseErrorV( type, context, message, ap );
	va_end( ap );
}

void genesisRaiseUGCError(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, const char *message_key, char* message, ...)
{
	va_list ap;
	va_start( ap, message );
	genesisRaiseUGCErrorV( type, context, NULL, message_key, NULL, message, ap );
	va_end( ap );
}

void genesisRaiseUGCErrorInField(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, const char* fieldName, const char *message_key, char* message, ...)
{
	va_list ap;
	va_start( ap, message );
	genesisRaiseUGCErrorV( type, context, fieldName, message_key, NULL, message, ap );
	va_end( ap );
}

void genesisRaiseUGCErrorInFieldExtraText(GenesisRuntimeErrorType type, GenesisRuntimeErrorContext* context, const char* fieldName, const char *message_key, const char* extra_text, char* message, ...)
{
	va_list ap;
	va_start( ap, message );
	genesisRaiseUGCErrorV( type, context, fieldName, message_key, extra_text, message, ap );
	va_end( ap );
}

void genesisRaiseErrorInternal(GenesisRuntimeErrorType type, const char* dict_name, const char *object_name, char *message, ...)
{
	va_list ap;
	GenesisRuntimeErrorContext context = { 0 };
	context.scope = GENESIS_SCOPE_INTERNAL_DICT;
	context.dict_name = allocAddString(dict_name);
	context.resource_name = allocAddString(object_name);

	va_start( ap, message );
	genesisRaiseErrorV( type, &context, message, ap );
	va_end( ap );
}

void genesisRaiseErrorInternalCode(GenesisRuntimeErrorType type, char *message, ...)
{
	va_list ap;
	GenesisRuntimeErrorContext context = { 0 };
	char buffer[1024];
	context.scope = GENESIS_SCOPE_INTERNAL_CODE;

	va_start( ap, message );
	vsprintf(buffer, message, ap );
	genesisRaiseError( type, &context, "%s", buffer );
	va_end( ap );
}

static GenesisRuntimeErrorContext g_ctx = { 0 };

static GenesisRuntimeErrorContext* genesisMakeErrorScopeCommon(GenesisRuntimeErrorScope scope, bool alloc MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx;
	if (alloc)
		ctx = StructCreate_dbg( parse_GenesisRuntimeErrorContext, __FUNCTION__ MEM_DBG_PARMS_CALL );
	else
		ctx = &g_ctx;
		StructReset( parse_GenesisRuntimeErrorContext, ctx );
	ctx->scope = scope;
	ctx->map_name = StructAllocString_dbg( gCurrentMap, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextDefault_Internal(bool alloc MEM_DBG_PARMS)
{
	return genesisMakeErrorScopeCommon(GENESIS_SCOPE_DEFAULT, alloc MEM_DBG_PARMS_CALL);
}

GenesisRuntimeErrorContext* genesisMakeErrorContextMap_Internal(bool alloc, const char *map_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_MAP, alloc MEM_DBG_PARMS_CALL);
	ctx->map_name = StructAllocString_dbg( map_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextRoom_Internal(bool alloc, const char *room_name, const char *layout_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_ROOM, alloc MEM_DBG_PARMS_CALL);
	ctx->location_name = StructAllocString_dbg( room_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextRoomDoor_Internal(bool alloc, const char *challenge_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_ROOM_DOOR, alloc MEM_DBG_PARMS_CALL);
	ctx->challenge_name = StructAllocString_dbg( challenge_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextPath_Internal(bool alloc, const char *path_name, const char *layout_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_PATH, alloc MEM_DBG_PARMS_CALL);
	ctx->location_name = StructAllocString_dbg( path_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextChallenge_Internal(bool alloc, const char *challenge_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_CHALLENGE, alloc MEM_DBG_PARMS_CALL);
	ctx->challenge_name = StructAllocString_dbg( challenge_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextObjective_Internal(bool alloc, const char *objective_name, const char *mission_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_OBJECTIVE, alloc MEM_DBG_PARMS_CALL);
	ctx->objective_name = StructAllocString_dbg( objective_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextPrompt_Internal(bool alloc, const char *prompt_name, const char *block_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_PROMPT, alloc MEM_DBG_PARMS_CALL);
	ctx->prompt_name = StructAllocString_dbg( prompt_name, caller_fname, line );
	ctx->prompt_block_name = StructAllocString_dbg( block_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextLockedDoor_Internal(bool alloc, const char *target_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_PROMPT, alloc MEM_DBG_PARMS_CALL);
	ctx->challenge_name = StructAllocString_dbg( target_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextMission_Internal(bool alloc, const char *mission_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_MISSION, alloc MEM_DBG_PARMS_CALL);
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextPortal_Internal(bool alloc, const char *portal_name, const char *mission_name, const char *layout_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_PORTAL, alloc MEM_DBG_PARMS_CALL);
	ctx->portal_name = StructAllocString_dbg( portal_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextLayout_Internal(bool alloc, const char *layout_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_LAYOUT, alloc MEM_DBG_PARMS_CALL);
	ctx->layout_name = StructAllocString_dbg( layout_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextEpisodePart_Internal(bool alloc, const char *episode_part MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_EPISODE_PART, alloc MEM_DBG_PARMS_CALL);
	ctx->ep_part_name = StructAllocString_dbg( episode_part, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextSolSysDetailObject_Internal(bool alloc, const char *detail_object_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_SOLSYS_DETAIL_OBJECT, alloc MEM_DBG_PARMS_CALL);
	ctx->solsys_detail_object_name = StructAllocString_dbg( detail_object_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextDictionary_Internal(bool alloc, const char *dict_name, const char *res_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_INTERNAL_DICT, alloc MEM_DBG_PARMS_CALL);
	ctx->dict_name = StructAllocString_dbg( dict_name, caller_fname, line );
	ctx->resource_name = StructAllocString_dbg( res_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextMapTransition_Internal(bool alloc, const char *objective_name, const char *mission_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_MAP_TRANSITION, alloc MEM_DBG_PARMS_CALL);
	ctx->objective_name = StructAllocString_dbg( objective_name, caller_fname, line );
	ctx->mission_name = StructAllocString_dbg( mission_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorContextUGCItem_Internal(bool alloc, const char *item_name MEM_DBG_PARMS)
{
	GenesisRuntimeErrorContext* ctx = genesisMakeErrorScopeCommon(GENESIS_SCOPE_UGC_ITEM, alloc MEM_DBG_PARMS_CALL);
	ctx->ugc_item_name = StructAllocString_dbg( item_name, caller_fname, line );
	return ctx;
}

GenesisRuntimeErrorContext* genesisMakeErrorAutoGen(GenesisRuntimeErrorContext *ctx)
{
	ctx->auto_generated = true;
	return ctx;
}

void genesisErrorPrintContextStr( const GenesisRuntimeErrorContext* context, char* text, size_t text_size )
{
	char auto_generated[256];
	char mission_context[256];

	if( context->auto_generated ) {
		sprintf( auto_generated, "AUTOGENERATED " );
	} else {
		sprintf( auto_generated, "" );
	}
	if( context->mission_name ) {
		sprintf( mission_context, "Mission: %s,", context->mission_name );
	} else {
		sprintf( mission_context, "Shared" );
	}
	
	switch( context->scope ) {
		case GENESIS_SCOPE_DEFAULT:
			sprintf_s(SAFESTR2(text), "");
		xcase GENESIS_SCOPE_MAP:
			sprintf_s(SAFESTR2(text), "%sMap", auto_generated);
		xcase GENESIS_SCOPE_ROOM:
			sprintf_s(SAFESTR2(text), "%sLayout: %s, Room: %s", auto_generated, context->layout_name, context->location_name);
		xcase GENESIS_SCOPE_PATH:
			sprintf_s(SAFESTR2(text), "%sLayout: %s, Path: %s", auto_generated, context->layout_name, context->location_name);
		xcase GENESIS_SCOPE_CHALLENGE:
			sprintf_s(SAFESTR2(text), "%s%s Layout: %s, Challenge: %s", auto_generated, mission_context, context->layout_name, context->challenge_name);
		xcase GENESIS_SCOPE_OBJECTIVE:
			sprintf_s(SAFESTR2(text), "%s%s Objective: %s", auto_generated, mission_context, context->objective_name);
		xcase GENESIS_SCOPE_PROMPT:
			sprintf_s(SAFESTR2(text), "%s%s Layout: %s, Prompt: %s", auto_generated, mission_context, context->layout_name, context->prompt_name);
		xcase GENESIS_SCOPE_PORTAL:
			sprintf_s(SAFESTR2(text), "%s%s Layout: %s, Portal: %s", auto_generated, mission_context, context->layout_name, context->portal_name);
		xcase GENESIS_SCOPE_MISSION:
			sprintf_s(SAFESTR2(text), "%sMission: %s", auto_generated, context->mission_name);
		xcase GENESIS_SCOPE_LAYOUT:
			sprintf_s(SAFESTR2(text), "%sLayout: %s", auto_generated, context->layout_name);
		xcase GENESIS_SCOPE_SOLSYS_DETAIL_OBJECT:
			sprintf_s(SAFESTR2(text), "%sDetail Obj: %s", auto_generated, context->solsys_detail_object_name);
		xcase GENESIS_SCOPE_EPISODE_PART:
			sprintf_s(SAFESTR2(text), "%sEp Part: %s", auto_generated, context->ep_part_name);

		xcase GENESIS_SCOPE_INTERNAL_DICT:
			sprintf_s(SAFESTR2(text), "Dict %s: %s", context->dict_name, context->resource_name);
		xcase GENESIS_SCOPE_INTERNAL_CODE:
			sprintf_s(SAFESTR2(text), "Internal Error" );

		xdefault:
			sprintf_s(SAFESTR2(text), "Unknown Component");
	}
}

void genesisErrorPrint( const GenesisRuntimeError* error, char* text, size_t text_size )
{
	char contextText[ 256 ] = "";
	genesisErrorPrintContextStr( error->context, SAFESTR( contextText ));
	
	switch( error->type ) {
		case GENESIS_FATAL_ERROR:
			sprintf_s( SAFESTR2( text ), "FATAL ERROR: %s%s%s",
					   contextText, (contextText[0] ? " -- " : ""),
					   error->message );
			
		xcase GENESIS_ERROR:
			sprintf_s( SAFESTR2( text ), "ERROR: %s%s%s",
					   contextText, (contextText[0] ? " -- " : ""),
					   error->message );

		xcase GENESIS_WARNING:
			sprintf_s( SAFESTR2( text ), "WARNING: %s%s%s",
					   contextText, (contextText[0] ? " -- " : ""),
					   error->message );

		xdefault:
			sprintf_s( SAFESTR2( text ), "UNKNOWN: %s%s%s",
					   contextText, (contextText[0] ? " -- " : ""),
					   error->message );
	}
}


//////////////////////////////////////////////////////////////////
// Naming conventions
//////////////////////////////////////////////////////////////////

const char *genesisMissionRoomVolumeName(const char* layout_name, const char *room_name, const char *mission_name)
{
	char buf[256];
	sprintf(buf, "%s_%s_%s", layout_name, mission_name, room_name);
	return allocAddString(buf);
}

const char *genesisMissionChallengeVolumeName(const char *challenge_name, const char *mission_name)
{
	char buf[256];
	sprintf(buf, "%s_VOLUME", challenge_name);
	return allocAddString(buf);
}

const char *genesisMissionVolumeName(const char* layout_name, const char *mission_name)
{
	char buf[256];
	sprintf(buf, "%s_%s_OptActs", layout_name, mission_name);
	return allocAddString(buf);
}


//////////////////////////////////////////////////////////////////
// Misc Query functions
//////////////////////////////////////////////////////////////////

void genesisDestroyStateData(void)
{
	#ifndef NO_EDITORS
	{
		StructDestroySafe(parse_GenesisZoneNodeLayout, &wl_state.genesis_node_layout);
	}
	#endif
}

bool genesisUnfreezeDisabled(void)
{
	#ifndef NO_EDITORS
	{
		static bool inited=false;
		if (!inited)
		{
			gGenesisUnfreezeDisabled
				= (!gimmeDLLQueryExists()
				   || !(UserIsInGroupEx( "Software", true ) || UserIsInGroupEx( "GenesisMasters", true )));
			inited = true;
		}
		return gGenesisUnfreezeDisabled;
	}
	#else
	{
		return true;
	}
	#endif
	
}


//////////////////////////////////////////////////////////////////
// Fixup functions
//////////////////////////////////////////////////////////////////

static void fixupGenesisMapDescriptionPre0(GenesisMapDescription *pMapDesc)
{
	if( pMapDesc->version_0.backdrop_info.old_backdrop_tags ) {
		eaDestroyEx( &pMapDesc->version_0.backdrop_info.backdrop_tag_list, StructFreeString );
		DivideString( pMapDesc->version_0.backdrop_info.old_backdrop_tags, ",", &pMapDesc->version_0.backdrop_info.backdrop_tag_list,
					  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
		StructFreeStringSafe( &pMapDesc->version_0.backdrop_info.old_backdrop_tags );
	}
}

bool genesisLayoutNameExists(GenesisMapDescription *pMapDesc, const char *pcName, void *pExcludeLayout)
{
	int i;
	for ( i=0; i < eaSize(&pMapDesc->interior_layouts); i++ )
	{
		if(pMapDesc->interior_layouts[i] == pExcludeLayout)
			continue;
		if(stricmp_safe(pMapDesc->interior_layouts[i]->name, pcName) == 0)
			return true;
	}
	for ( i=0; i < eaSize(&pMapDesc->solar_system_layouts); i++ )
	{
		if(pMapDesc->solar_system_layouts[i] == pExcludeLayout)
			continue;
		if(stricmp_safe(pMapDesc->solar_system_layouts[i]->name, pcName) == 0)
			return true;
	}
	if(	pMapDesc->exterior_layout && 
		pMapDesc->exterior_layout != pExcludeLayout && 
		stricmp_safe(pMapDesc->exterior_layout->name, pcName) == 0)
		return true;
	return false;
}

char* genesisMakeNewLayoutName(GenesisMapDescription *pMapDesc, GenesisMapType layer_type)
{
	int i;
	char new_name[128];

	for ( i=0; i < 100000; i++ )
	{
		switch(layer_type)
		{
		case GenesisMapType_None:
		case GenesisMapType_SolarSystem:
			sprintf(new_name, "SolarSystem_%02d", i+1);
			break;
		case GenesisMapType_Exterior:
			sprintf(new_name, "Exterior_%02d", i+1);
			break;
		case GenesisMapType_Interior:
			sprintf(new_name, "Interior_%02d", i+1);
			break;
		}
		if(!genesisLayoutNameExists(pMapDesc, new_name, NULL))
			return StructAllocString(new_name);
	}
	assert(false);
	return NULL;
}

static void fixupGenesisMapDescription0to1(GenesisMapDescription *pMapDesc)
{
	int i, j;
	GenesisLayoutCommonData *pCommonData=NULL;
	char *pcLayoutName=NULL;

	switch(pMapDesc->version_0.map_type)
	{
	case GenesisMapType_SolarSystem:
		if(	eaSize(&pMapDesc->solar_system_layouts)==0 &&
			eaSize(&pMapDesc->interior_layouts)==0 && 
			!pMapDesc->exterior_layout) {
			return;//Must just be map that is not filled out at all
		}

		if(eaSize(&pMapDesc->solar_system_layouts)==0) {
			ErrorFilenamef(pMapDesc->filename, "MapDescripion of version %d, is type Solar System but no layout exists.", pMapDesc->version);
			return;
		}
		if(eaSize(&pMapDesc->solar_system_layouts) > 1) {
			ErrorFilenamef(pMapDesc->filename, "MapDescripion of version %d, has more than one Solar System layout.", pMapDesc->version);
			return;
		}
		pCommonData = &pMapDesc->solar_system_layouts[0]->common_data;
		if(!pMapDesc->solar_system_layouts[0]->name)
			pMapDesc->solar_system_layouts[0]->name = genesisMakeNewLayoutName(pMapDesc, pMapDesc->version_0.map_type);
		pcLayoutName = pMapDesc->solar_system_layouts[0]->name;
		break;
	case GenesisMapType_Exterior:
		if(!pMapDesc->exterior_layout) {
			ErrorFilenamef(pMapDesc->filename, "MapDescripion of version %d, is type Exterior but no layout exists.", pMapDesc->version);
			return;
		}
		pMapDesc->exterior_layout->layout_info_specifier = pMapDesc->version_0.layout_info_specifier;
		COPY_HANDLE(pMapDesc->exterior_layout->ext_template, pMapDesc->version_0.ext_template);
		pCommonData = &pMapDesc->exterior_layout->common_data;
		if(!pMapDesc->exterior_layout->name)
			pMapDesc->exterior_layout->name = genesisMakeNewLayoutName(pMapDesc, pMapDesc->version_0.map_type);
		pcLayoutName = pMapDesc->exterior_layout->name;
		break;
	case GenesisMapType_Interior:
		if(eaSize(&pMapDesc->interior_layouts)==0) {
			ErrorFilenamef(pMapDesc->filename, "MapDescripion of version %d, is type Interior but no layout exists.", pMapDesc->version);
			return;
		}
		if(eaSize(&pMapDesc->interior_layouts) > 1) {
			ErrorFilenamef(pMapDesc->filename, "MapDescripion of version %d, has more than one Interior layout.", pMapDesc->version);
			return;
		}
		pMapDesc->interior_layouts[0]->layout_info_specifier = pMapDesc->version_0.layout_info_specifier;
		COPY_HANDLE(pMapDesc->interior_layouts[0]->int_template, pMapDesc->version_0.int_template);
		pCommonData = &pMapDesc->interior_layouts[0]->common_data;
		if(!pMapDesc->interior_layouts[0]->name)
			pMapDesc->interior_layouts[0]->name = genesisMakeNewLayoutName(pMapDesc, pMapDesc->version_0.map_type);
		pcLayoutName = pMapDesc->interior_layouts[0]->name;
		break;
	default:
		ErrorFilenamef(pMapDesc->filename, "MapDescripion is version %d, but map type is not of known type.", pMapDesc->version);
		return;
	}

	StructCopyAll(parse_GenesisMapDescBackdrop, &pMapDesc->version_0.backdrop_info, &pCommonData->backdrop_info);
	pCommonData->no_sharing_detail = pMapDesc->version_0.no_sharing_detail;
	StructCopyAll(parse_GenesisEncounterJitter, &pMapDesc->version_0.jitter, &pCommonData->jitter);

	assert(pcLayoutName);
	for ( i=0; i < eaSize(&pMapDesc->missions); i++ )
	{
		GenesisMissionDescription *mission = pMapDesc->missions[i];
		for ( j=0; j < eaSize(&mission->eaChallenges); j++ )
			mission->eaChallenges[j]->pcLayoutName = StructAllocString(pcLayoutName);
		for ( j=0; j < eaSize(&mission->zoneDesc.eaPortals); j++ )
			mission->zoneDesc.eaPortals[j]->pcStartLayout = StructAllocString(pcLayoutName);
		for ( j=0; j < eaSize(&mission->zoneDesc.eaPrompts); j++ )
			mission->zoneDesc.eaPrompts[j]->pcLayoutName = StructAllocString(pcLayoutName);
	}
	for ( i=0; i < eaSize(&pMapDesc->shared_challenges); i++ )
		pMapDesc->shared_challenges[i]->pcLayoutName = StructAllocString(pcLayoutName);
}

static void fixupGenesisMapDescription1to2When( GenesisWhen* pWhen, void* ignored1, const char* ignored2, char* defaultLayoutName )
{
	int it;
	for( it = 0; it != eaSize( &pWhen->eaRooms ); ++it ) {
		GenesisWhenRoom* whenRoom = pWhen->eaRooms[ it ];
		if( !whenRoom->layoutName && whenRoom->roomName ) {
			whenRoom->layoutName = StructAllocString( defaultLayoutName );
		}
	}
}

static void fixupGenesisMapDescription1to2Challenge( GenesisMissionChallenge* pChallenge, void* ignored1, const char* ignored2, char* defaultLayoutName )
{
	fixupGenesisMapDescription1to2When(&pChallenge->spawnWhen, ignored1, ignored2, defaultLayoutName );
}

static void fixupGenesisMapDescription1to2Prompt( GenesisMissionPrompt* pPrompt, void* ignored1, const char* ignored2, char* defaultLayoutName )
{
	fixupGenesisMapDescription1to2When(&pPrompt->showWhen, ignored1, ignored2, defaultLayoutName );
}

static void fixupGenesisMapDescription1to2(GenesisMapDescription *pMapDesc)
{
	bool bOneLayoutExists = false;
	const char* defaultLayoutName = NULL;
	
	if (eaSize(&pMapDesc->solar_system_layouts) + eaSize(&pMapDesc->interior_layouts) + (pMapDesc->exterior_layout != NULL) > 1) {
		ErrorFilenamef(pMapDesc->filename, "MapDescripion is version %d, but there are multiple layouts.", pMapDesc->version);
		return;
	}

	if( !defaultLayoutName && eaSize( &pMapDesc->solar_system_layouts )) {
		bOneLayoutExists = true;
		defaultLayoutName = pMapDesc->solar_system_layouts[0]->name;
	}
	if( !defaultLayoutName && eaSize( &pMapDesc->interior_layouts )) {
		bOneLayoutExists = true;
		defaultLayoutName = pMapDesc->interior_layouts[0]->name;
	}
	if( !defaultLayoutName && pMapDesc->exterior_layout ) {
		bOneLayoutExists = true;
		defaultLayoutName = pMapDesc->exterior_layout->name;
	}
	if(!bOneLayoutExists)
		return;
	if( !defaultLayoutName ) {
		ErrorFilenamef(pMapDesc->filename, "MapDescription is version %d, but none of the layouts have names.", pMapDesc->version);
		return;
	}

	// Fixup whens -- have to cast away const on defaultLayoutName to
	// make the compiler happy. :(
	ParserScanForSubstruct(parse_GenesisMapDescription, pMapDesc, parse_GenesisWhen, 0, 0,
						   (ParserScanForSubstructCB)fixupGenesisMapDescription1to2When,
						   (void*)defaultLayoutName );
	ParserScanForSubstruct(parse_GenesisMapDescription, pMapDesc, parse_GenesisMissionChallenge, 0, 0,
						   (ParserScanForSubstructCB)fixupGenesisMapDescription1to2Challenge,
						   (void*)defaultLayoutName );
	ParserScanForSubstruct(parse_GenesisMapDescription, pMapDesc, parse_GenesisMissionPrompt, 0, 0,
						   (ParserScanForSubstructCB)fixupGenesisMapDescription1to2Prompt,
						   (void*)defaultLayoutName );
	
	// Fixup mission start desc
	{
		int it;
		for( it = 0; it != eaSize( &pMapDesc->missions ); ++it ) {

			{
				GenesisMissionStartDescription* startDesc = &pMapDesc->missions[ it ]->zoneDesc.startDescription;
				if( startDesc->pcStartRoom && !startDesc->pcStartLayout ) {
					startDesc->pcStartLayout = StructAllocString( defaultLayoutName );
				}
				if( startDesc->pcExitRoom && !startDesc->pcExitLayout ) {
					startDesc->pcExitLayout = StructAllocString( defaultLayoutName );
				}
				if( startDesc->pcContinueRoom && !startDesc->pcContinueLayout ) {
					startDesc->pcContinueLayout = StructAllocString( defaultLayoutName );
				}
			}
		}
	}
}

/// Fixup function for GenesisMapDescription
TextParserResult fixupGenesisMapDescription(GenesisMapDescription *pMapDesc, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup tags into new format
			if(pMapDesc->version < 1) {
				fixupGenesisMapDescriptionPre0(pMapDesc);
				fixupGenesisMapDescription0to1(pMapDesc);
			}
			if(pMapDesc->version < 2) {
				fixupGenesisMapDescription1to2(pMapDesc);
			}
			pMapDesc->version = GENESIS_MAP_DESC_VERSION;
		}
	}
	
	return PARSERESULT_SUCCESS;
}

/// Fixup function for GenesisDetailKitLayout
TextParserResult fixupGenesisDetailKitLayout(GenesisDetailKitLayout *pDetailKit, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup tags into new format
			{
				if( pDetailKit->old_detail_tags ) {
					eaDestroyEx( &pDetailKit->detail_tag_list, StructFreeString );
					DivideString( pDetailKit->old_detail_tags, ",", &pDetailKit->detail_tag_list,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pDetailKit->old_detail_tags );
				}
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}

/// Fixup function for GenesisDetailKitLayout
TextParserResult fixupGenesisRoomDetailKitLayout(GenesisRoomDetailKitLayout *pDetailKit, enumTextParserFixupType eType, void *pExtraData)
{
	switch (eType)
	{
		case FIXUPTYPE_POST_TEXT_READ: case FIXUPTYPE_POST_BIN_READ: {
			// Fixup tags into new format
			{
				if( pDetailKit->old_detail_tags ) {
					eaDestroyEx( &pDetailKit->detail_tag_list, StructFreeString );
					DivideString( pDetailKit->old_detail_tags, ",", &pDetailKit->detail_tag_list,
								  DIVIDESTRING_POSTPROCESS_STRIP_WHITESPACE | DIVIDESTRING_POSTPROCESS_DONT_PUSH_EMPTY_STRINGS );
					StructFreeStringSafe( &pDetailKit->old_detail_tags );
				}
			}
		}
	}
	
	return PARSERESULT_SUCCESS;
}

#include "wlGenesis_h_ast.c"
