#define GENESIS_ALLOW_OLD_HEADERS
#include "Genesis.h"

#include "ResourceManager.h"
#include "SimpleParser.h"
#include "StringUtil.h"
#include "WorldGrid.h"
#include "earray.h"
#include "Entity.h"
#include "error.h"
#include "file.h"
#include "wlGenesis.h"
#include "mission_common.h"

#include"AutoGen/GameClientLib_autogen_ClientCmdWrappers.h"

typedef bool (*ZoneMapQueryFn)( ZoneMapInfo* zmapInfo );
static ZoneMapInfo** genesisReadZoneMaps( const char* fileName, ZoneMapQueryFn fn )
{
	ZoneMapInfo** zmapInfos = NULL;
	FILE* file = fopen( fileName, "r" );
	char zmapName[ 256 ];
	int lineIt = 0;

	if( !file ) {
		genesisRaiseErrorInternalCode( GENESIS_FATAL_ERROR, "File: %s -- Script file does not exist.", fileName );
		return NULL;
	}
	while( fgets( zmapName, sizeof( zmapName ), file )) {
		if( !StringIsAllWhiteSpace( zmapName )) {
			const char* buffer;

			forwardSlashes( zmapName );
			removeTrailingWhiteSpaces( zmapName );
			buffer = removeLeadingWhiteSpaces( zmapName );
			
			if( strEndsWith( buffer, "/" )) {
				// is a directory, get all zone maps, recursively
				RefDictIterator it = { 0 };
				ZoneMapInfo* zmapInfo;
				int numMaps = 0;
				worldGetZoneMapIterator( &it );
				while( zmapInfo = worldGetNextZoneMap( &it )) {
					const char* filename = zmapInfoGetFilename( zmapInfo );
					if( strStartsWith( filename, buffer ) && (fn == NULL || fn( zmapInfo ))) {
						eaPush( &zmapInfos, StructClone( parse_ZoneMapInfo, zmapInfo ));
						++numMaps;
					}
				}

				if( numMaps == 0 ) {
					genesisRaiseErrorInternalCode( GENESIS_FATAL_ERROR, "Line: %d -- Could not find any zonemaps in folder %s",
												   lineIt + 1, buffer );
				}
			} else {
				ZoneMapInfo* zmapInfo = worldGetZoneMapByPublicName( buffer );
				if( zmapInfo ) {
					if( fn == NULL || fn( zmapInfo )) {
						eaPush( &zmapInfos, StructClone( parse_ZoneMapInfo, zmapInfo ));
					} else {
						genesisRaiseErrorInternalCode( GENESIS_WARNING, "Line: %d -- Zonemap appears to be already processed, ignoring",
													   lineIt + 1 );
					}
				} else {
					genesisRaiseErrorInternalCode( GENESIS_FATAL_ERROR, "Line: %d -- Could not find zonemap %s",
												   lineIt + 1, buffer );
				}
			}
		}

		++lineIt;
	}
	fclose( file );

	return zmapInfos;
}

static void genesisLockZoneMaps( ZoneMapInfo** zmapInfos )
{
	ResourceActionList actions = { 0 };
	int it;
	for( it = 0; it != eaSize( &zmapInfos ); ++it ) {
		ZoneMapInfo* zmInfo = zmapInfos[ it ];
		zmapInfoLockQueueActions( &actions, zmInfo, true );
	}

	resRequestResourceActions( &actions );
	if( actions.eResult != kResResult_Success ) {
		for( it = 0; it != eaSize( &actions.ppActions ); ++it ) {
			if( actions.ppActions[ it ]->eResult != kResResult_Success ) {
				genesisRaiseErrorInternal( GENESIS_FATAL_ERROR, "ZoneMap", actions.ppActions[ it ]->pResourceName,
										   "%s", actions.ppActions[ it ]->estrResultString );
			}
		}
	}
	StructDeInit( parse_ResourceActionList, &actions );
}

AUTO_COMMAND ACMD_SERVERCMD;
void genesisTransmogrifyMaps( Entity* ent, const char* fileName )
{
	GenesisRuntimeStatus* status = StructCreate( parse_GenesisRuntimeStatus );
	ZoneMapInfo** zmapInfos = NULL;
	int iPartitionIdx = entGetPartitionIdx(ent);
	
	resSetDictionaryEditModeServer( "ZoneMap", true );

	// Get all the zone maps
	genesisSetStageAndAdd( status, "Lock Zonemaps" );
	zmapInfos = genesisReadZoneMaps( fileName, zmapInfoHasGenesisData );

	// Lock all the zonemaps
	genesisLockZoneMaps( zmapInfos );

	// Reseed all the zonemaps
	if( !genesisStatusFailed( status )) {
		genesisReseedExternalMapDescs( iPartitionIdx, status, zmapInfos, false, false );
	}

	ClientCmd_genesisDisplayRuntimeStatus( ent, status );
	eaDestroyStruct( &zmapInfos, parse_ZoneMapInfo );
	StructDestroy( parse_GenesisRuntimeStatus, status );
}

AUTO_COMMAND ACMD_SERVERCMD;
void genesisReseedMaps( Entity* ent, const char* fileName, bool reseedAll )
{
	GenesisRuntimeStatus* status = StructCreate( parse_GenesisRuntimeStatus );
	ZoneMapInfo** zmapInfos = NULL;
	int iPartitionIdx = entGetPartitionIdx(ent);
	
	resSetDictionaryEditModeServer( "ZoneMap", true );

	// Get all the zone maps
	genesisSetStageAndAdd( status, "Lock Zonemaps" );
	zmapInfos = genesisReadZoneMaps( fileName, zmapInfoHasGenesisData );

	// Lock all the zonemaps
	genesisLockZoneMaps( zmapInfos );

	// Reseed all the zonemaps
	if( !genesisStatusFailed( status )) {
		genesisReseedExternalMapDescs( iPartitionIdx, status, zmapInfos, reseedAll, true );
	}

	ClientCmd_genesisDisplayRuntimeStatus( ent, status );
	eaDestroyStruct( &zmapInfos, parse_ZoneMapInfo );
	StructDestroy( parse_GenesisRuntimeStatus, status );
}

AUTO_COMMAND ACMD_SERVERCMD;
void genesisFreezeMaps( Entity* ent, const char* fileName )
{
	GenesisRuntimeStatus* status = StructCreate( parse_GenesisRuntimeStatus );
	ZoneMapInfo** zmapInfos = NULL;
	
	resSetDictionaryEditModeServer( "ZoneMap", true );

	// Get all the zone maps
	genesisSetStageAndAdd( status, "Lock Zonemaps" );
	zmapInfos = genesisReadZoneMaps( fileName, zmapInfoHasGenesisData );

	// Lock all the zonemaps
	genesisLockZoneMaps( zmapInfos );

	// Reseed all the zonemaps
	if( !genesisStatusFailed( status )) {
		genesisFreezeExternalMapDescs( entGetPartitionIdx(ent), status, zmapInfos, false );
	}

	ClientCmd_genesisDisplayRuntimeStatus( ent, status );
	eaDestroyStruct( &zmapInfos, parse_ZoneMapInfo );
	StructDestroy( parse_GenesisRuntimeStatus, status );
}


AUTO_COMMAND ACMD_SERVERCMD;
void genesisFreezeVistaMaps( Entity* ent, const char* fileName )
{
	GenesisRuntimeStatus* status = StructCreate( parse_GenesisRuntimeStatus );
	ZoneMapInfo** zmapInfos = NULL;
	
	resSetDictionaryEditModeServer( "ZoneMap", true );

	// Get all the zone maps
	genesisSetStageAndAdd( status, "Lock Zonemaps" );
	zmapInfos = genesisReadZoneMaps( fileName, zmapInfoHasGenesisData );

	// Lock all the zonemaps
	genesisLockZoneMaps( zmapInfos );

	// Reseed all the zonemaps
	if( !genesisStatusFailed( status )) {
		genesisFreezeExternalMapDescs( entGetPartitionIdx(ent), status, zmapInfos, true );
	}

	ClientCmd_genesisDisplayRuntimeStatus( ent, status );
	eaDestroyStruct( &zmapInfos, parse_ZoneMapInfo );
	StructDestroy( parse_GenesisRuntimeStatus, status );
}

AUTO_COMMAND ACMD_SERVERCMD;
void genesisUnfreezeMaps( Entity* ent, const char* fileName )
{
	GenesisRuntimeStatus* status = StructCreate( parse_GenesisRuntimeStatus );
	ZoneMapInfo** zmapInfos = NULL;
	
	resSetDictionaryEditModeServer( "ZoneMap", true );

	// Get all the zone maps
	genesisSetStageAndAdd( status, "Lock Zonemaps" );
	zmapInfos = genesisReadZoneMaps( fileName, zmapInfoHasBackupGenesisData );

	// Lock all the zonemaps
	genesisLockZoneMaps( zmapInfos );

	// Reseed all the zonemaps
	if( !genesisStatusFailed( status )) {
		genesisUnfreezeExternalMapDescs( status, zmapInfos );
	}

	ClientCmd_genesisDisplayRuntimeStatus( ent, status );
	eaDestroyStruct( &zmapInfos, parse_ZoneMapInfo );
	StructDestroy( parse_GenesisRuntimeStatus, status );
}

AUTO_COMMAND ACMD_SERVERCMD;
void genesisSetMapsType( Entity* ent, const char* fileName, ACMD_NAMELIST(ZoneMapTypeEnum, STATICDEFINE) ZoneMapType zmtype )
{
	GenesisRuntimeStatus* status = StructCreate( parse_GenesisRuntimeStatus );
	ZoneMapInfo** zmapInfos = NULL;

	resSetDictionaryEditModeServer( "ZoneMap", true );
	
	// Get all the zone maps
	genesisSetStageAndAdd( status, "Lock Zonemaps" );
	zmapInfos = genesisReadZoneMaps( fileName, NULL );

	// Lock all the zonemaps
	genesisLockZoneMaps( zmapInfos );

	// Reseed all the zonemaps
	if( !genesisStatusFailed( status )) {
		genesisSetExternalMapsType( status, zmapInfos, zmtype );
	}

	ClientCmd_genesisDisplayRuntimeStatus( ent, status );
	eaDestroyStruct( &zmapInfos, parse_ZoneMapInfo );
	StructDestroy( parse_GenesisRuntimeStatus, status );
}

// Not strictly genesis, but needed only for genesis maps
AUTO_COMMAND ACMD_SERVERCMD;
void genesisScanForBadStarClusterMissions(void)
{
	FILE* log = fopen( "c:/BadStarClusterMissions.txt", "w" );
	int numBadMissionsFound = 0;

	FOR_EACH_IN_REFDICT( g_MissionDictionary, MissionDef, mission ) {
		if( !strstri( mission->filename, "Star_Cluster" ) || mission->missionType == MissionType_OpenMission ) {
			continue;
		}

		if( mission->eShareable != MissionShareableType_Never ) {
			fprintf( log, "%s\n", mission->filename );
			++numBadMissionsFound;
		}
	} FOR_EACH_END;

	fclose( log );

	Alertf( "%d bad star cluster missions found.  Logged in C:/BadStarClusterMissions.txt", numBadMissionsFound );
}

AUTO_COMMAND;
void genesisDebugRebuild(Entity *pEnt)
{
	genesisRebuildLayers(entGetPartitionIdx(pEnt), worldGetActiveMap(), false);
}

