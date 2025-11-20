#include"gclResourceSnap.h"

#include"Color.h"
#include"CostumeCommon.h"
#include"DirectDrawTypes.h"
#include"GfxCommonSnap.h"
#include"GfxDebug.h"
#include"GfxEditorIncludes.h"
#include"GfxHeadshot.h"
#include"GfxLoadScreens.h"
#include"GfxTextureTools.h"
#include"ObjectLibrary.h"
#include"RdrState.h"
#include"StashTable.h"
#include"StringUtil.h"
#include"UtilitiesLib.h"
#include"WorldGrid.h"
#include"WorldLib.h"
#include"gclUIGen.h"
#include"tga.h"
#include"timing.h"
#include"wlSaveDXT.h"
#include "MapSnap.h"
#include "wlState.h"

AUTO_RUN_ANON( memBudgetAddMapping( __FILE__, BUDGET_Renderer ); );

#define RESOURCE_SNAP_VERSION 11

static bool g_isTakingPhotos = false;

bool g_ResourceSnapNoTimeout = false;
AUTO_CMD_INT( g_ResourceSnapNoTimeout, ResourceSnapNoTimeout ) ACMD_CMDLINE;
bool g_ResourceSnapForce = false;
AUTO_CMD_INT( g_ResourceSnapForce, ResourceSnapForce ) ACMD_CMDLINE;
bool g_ResourceSnapSaveImages = false;
AUTO_CMD_INT( g_ResourceSnapSaveImages, ResourceSnapSaveImages ) ACMD_CMDLINE;
bool g_ResourceSnapEnabled = true;
AUTO_CMD_INT( g_ResourceSnapEnabled, ResourceSnapEnabled ) ACMD_CMDLINE;
char g_strResourceSnapScope[ 256 ];
AUTO_CMD_STRING( g_strResourceSnapScope, ResourceSnapScope ) ACMD_CMDLINE;

#define PREVIEW_SKY "Ugc_Room_Preview"

/// State variable to detect a photo finishing
static bool g_isPhotoDone = false;

typedef struct ResourceSnapState {
	// Set once
	ResourceSnapDesc** eaResources;
	StashTable timestamps;

	// Set per resource
	ResourceSnapDesc* curDesc;
} ResourceSnapState;

AUTO_STRUCT;
typedef struct ResourceSnapTimestampFile {
	int version;
	int skyFileTimestamp;
} ResourceSnapTimestampFile;
extern ParseTable parse_ResourceSnapTimestampFile[];
#define TYPE_parse_ResourceSnapTimestampFile ResourceSnapTimestampFile

static void gclSnapSaveVersion( int version );
static void gclSnapCheckTimestamps( ResourceSnapState* state, const ResourceSnapTimestampFile* file );
static void gclSnapPhotoFinishedCB( ResourceSnapState* state, U8* cap_data, int w, int h, char** ignored );
static int gclSnapGetResourceTimestamp( ResourceSnapState* state, const ResourceSnapDesc* resource );
static int gclSnapGetSkyFileTimestamp( void );

bool gclResourceSnapTakePhotos( ResourceSnapDesc** eaResources )
{
	ResourceSnapState state = { 0 };
	GfxSnapSavedState origState;

	gfxLoadingSetLoadingMessage( "Taking Resource Photos" );
	gfxSnapOverrideState( &origState );

	g_isTakingPhotos = gfxHeadshotIsTakingResourceSnapPhotos = true;
	PERFINFO_AUTO_START_FUNC();
	{
		U32 startTime = timeSecondsSince2000_ForceRecalc();
		ResourceSnapTimestampFile tsFile = { 0 };
		
		gfxSkyClearAllVisibleSkies();
		eaCopy( &state.eaResources, &eaResources );

		state.timestamps = stashTableCreateWithStringKeys( 64, StashDeepCopyKeys | StashCaseInsensitive );

		ParserReadTextFile( "bin/images/timestamps.txt", parse_ResourceSnapTimestampFile, &tsFile, 0 );
		gclSnapCheckTimestamps( &state, &tsFile );

		if( g_ResourceSnapEnabled ) {
			int it;
			gfxSnapApplyOptions( true, false, false );
			gfCurrentMapOrthoSkewX = 0.0f;
			gfCurrentMapOrthoSkewZ = 0.0f;

			for( it = 0; it != eaSize( &state.eaResources ); ++it ) {
				const char* str;
				state.curDesc = state.eaResources[ it ];
				str = gclSnapGetResourceString( state.curDesc, false );

				if( !nullStr( g_strResourceSnapScope ) && !strstr( str, g_strResourceSnapScope )) {
					continue;
				}

				loadstart_printf( "Taking photo %d/%d: %s...",
								  it + 1, eaSize( &state.eaResources ), str );
				g_isPhotoDone = false;
				
				if( stricmp( state.curDesc->astrDictName, "ObjectLibrary" ) == 0 ) {
					GroupDef* def = objectLibraryGetGroupDefByName( state.curDesc->astrResName, true );
					int nearPlane;
					assert( def );

					nearPlane = def->bounds.max[ 1 ] + 5.f;
					if( state.curDesc->objectIsTopDownView ) {
						int width = def->bounds.max[ 0 ] - def->bounds.min[ 0 ];
						int height = def->bounds.max[ 2 ] - def->bounds.min[ 2 ];
						int textureWidth = CLAMP( pow2( width + 1 ), 4, 512 );
						int textureHeight = CLAMP( pow2( height + 1 ), 4, 512 );

						gfxHeadshotCaptureGroup( "ObjectSnapImage", textureWidth, textureHeight, def, NULL, 
												 ColorTransparent, GFX_HEADSHOT_OBJECT_FROM_ABOVE, NULL,
												 PREVIEW_SKY, nearPlane, false, true,
												 gclSnapPhotoFinishedCB, &state );
					} else {
						gfxHeadshotCaptureGroup( "ObjectSnapImage", 128, 128, def, NULL, 
												 ColorTransparent, GFX_HEADSHOT_OBJECT_AUTO, NULL,
												 PREVIEW_SKY, nearPlane, false, true,
												 gclSnapPhotoFinishedCB, &state );
					}
				} else if( stricmp( state.curDesc->astrDictName, "PlayerCostume" ) == 0 ) {
					BasicTexture* texture = gclHeadshotFromCostume(
							state.curDesc->astrHeadshotStyleDef, NULL, state.curDesc->astrResName, 128, 128,
							gclSnapPhotoFinishedCB, &state);

					if( !texture ) {
						Errorf( "Could not capture costume %s.", state.curDesc->astrResName );
						g_isPhotoDone = true;
					}
				}

				{
					int frameIt = 0;
					GfxDummyFrameInfo frame_loop_info = { 0 };

					gfxDummyFrameSequenceStart(&frame_loop_info);
					while( !g_isPhotoDone ) {
						gfxDummyFrameTopEx( &frame_loop_info, .01f, true );

						// [NNO-14978] Costume dynnodes are supposed to get dirtied once per
						// frame.  Need to update frame count to ensure this happens or else
						// headshot framing is unreliable.
						//
						// Brent TODO: Make this not needed, possibly by dirtying DynNodes
						// on skeleton create.
						++wl_state.frame_count;

						gfx_state.client_loop_timer = 0.0f;
						gfxLoadingDisplayScreen( true );
						gfxDummyFrameBottom( &frame_loop_info, NULL, true );
						Sleep( 20 );
						++frameIt;

						if( !g_ResourceSnapNoTimeout ) {
							//100 seconds @ 100 fps for this shot or 1 hr for the whole process.
							if( frameIt > 6000 || timeSecondsSince2000_ForceRecalc() - startTime > 3600 ) {
								break;
							}
						}
					}
					gfxDummyFrameSequenceEnd(&frame_loop_info);
				}
				loadend_printf( " done." );

				if( !g_isPhotoDone ) {
					Errorf( "Took too long to make preview images.  Client bins not fully made." );
					break;
				}
			}
			
			gfxSnapUndoOptions();
		} else {
			printf( "ResourceSnap disabled (for your sanity)\n" );
		}

		gclSnapSaveVersion( RESOURCE_SNAP_VERSION );

		// Delete intermediate files
		{
			char dir[ MAX_PATH ];
			char cmd[ MAX_PATH ];
			int rv;

			sprintf( dir, "%s/bin/object_library", fileTempDir() );
			if( dirExists( dir )) {
				backSlashes( dir );
				sprintf( cmd, "rd /s /q %s", dir );
				rv = system( cmd );
				if( rv != 0 ) {
					AssertOrAlert( "OBJSNAP_FILE_DELETE_FAILED", "Failed to remove object snap directory: %s", dir );
				}
			}
		}

		stashTableDestroy( state.timestamps );
		eaDestroy( &state.eaResources );
	}
	PERFINFO_AUTO_STOP();
	g_isTakingPhotos = gfxHeadshotIsTakingResourceSnapPhotos = false;
	gfxSnapRestoreState( &origState );

	return true;
}

void gclSnapPhotoFinishedCB( ResourceSnapState* state, U8* cap_data, int w, int h, char** ignored )
{
	U8 max_alpha = 0;
	{
		int idx;
		for( idx = 0; idx < w * h; idx++ ) {
			max_alpha = MAX( max_alpha, cap_data[ idx * 4 + 3 ]);
		}
	}

	if( !g_isTakingPhotos ) {
		return;
	}

	if (max_alpha < 64) {
		printf( "EMPTY IMAGE!\n" );
	}

	{
		int compressed_size = (w * h)*sizeof( U8 );
		int fileSize = compressed_size + 4 + sizeof( DDSURFACEDESC2 );
		U8* file = calloc( 1, fileSize );
		U8* fileIt = file;

		//File Type
		memcpy(fileIt, "DDS ", 4);
		fileIt += 4;

		//Header
		{
			DDSURFACEDESC2 header_data;
			memset( &header_data, 0, sizeof( DDSURFACEDESC2 ));
			if( state->curDesc->objectIsTopDownView ) {
				header_data.ddpfPixelFormat.dwFourCC = FOURCC_DXT5;
			} else {
				header_data.ddpfPixelFormat.dwFourCC = FOURCC_DXT1;
			}
			header_data.dwWidth = w;
			header_data.dwHeight = h;
			memcpy( fileIt, &header_data, sizeof( DDSURFACEDESC2 ));
			fileIt += sizeof( DDSURFACEDESC2 );
		}

		//Compress Data
		if( state->curDesc->objectIsTopDownView ) {
			nvdxtCompress( cap_data, fileIt, w, h, RTEX_DXT5, 1, 0 );
		} else {
			nvdxtCompress( cap_data, fileIt, w, h, RTEX_DXT1, 1, 0 );
		}

		//Write out file
		{
			char path[ MAX_PATH ];
			char fullPath[ MAX_PATH ];
			char timestampPath[ MAX_PATH ];
		
		
			sprintf( path, "bin/images/%s.wtex", gclSnapGetResourceString( state->curDesc, true ));
			binNotifyTouchedOutputFile( path );
		
			fileLocateWrite( path, fullPath );
			makeDirectoriesForFile( fullPath );
			texWriteData( fullPath, file, fileSize, NULL, w, h, true, TEXOPT_NOMIP | TEXOPT_CLAMPS | TEXOPT_CLAMPT, NULL, NULL );

			changeFileExt( fullPath, ".timestamp", timestampPath );
			fileForceRemove( timestampPath );
		}
		SAFE_FREE( file );
	}

	if( g_ResourceSnapSaveImages ) {
		char debug_filename[MAX_PATH];
		sprintf( debug_filename, "C:\\ResourceSnap\\%s.tga", gclSnapGetResourceString( state->curDesc, false ));

		// TGA needs the RGB order swapped
		{
			int it;
			for( it = 0; it < w * h; ++it ) {
				int r = cap_data[ it * 4 + 0 ];
				int g = cap_data[ it * 4 + 1 ];
				int b = cap_data[ it * 4 + 2 ];
				int a = cap_data[ it * 4 + 3 ];

				cap_data[ it * 4 + 0 ] = b;
				cap_data[ it * 4 + 1 ] = g;
				cap_data[ it * 4 + 2 ] = r;
				cap_data[ it * 4 + 3 ] = a;
			}
		}
		
		tgaSave( debug_filename, cap_data, w, h, 3 );
	}

	g_isPhotoDone = true;
}

void gclSnapSaveVersion( int version )
{
	ResourceSnapTimestampFile* timestampFile = StructCreate( parse_ResourceSnapTimestampFile );
	timestampFile->version = version;
	timestampFile->skyFileTimestamp = gclSnapGetSkyFileTimestamp();

	ParserWriteTextFile( "bin/images/timestamps.txt", parse_ResourceSnapTimestampFile, timestampFile, 0, 0 );
	binNotifyTouchedOutputFile( "bin/images/timestamps.txt" );

	StructDestroy( parse_ResourceSnapTimestampFile, timestampFile );
}

void gclSnapCheckTimestamps( ResourceSnapState* state, const ResourceSnapTimestampFile* file )
{
	int skyFileTimestamp = gclSnapGetSkyFileTimestamp();
	
	if( g_ResourceSnapForce ) {
		printf( "Taking all %d photos: Force flag is set.\n", eaSize( &state->eaResources ));
	} else if( file->version != RESOURCE_SNAP_VERSION ) {
		printf( "Taking all %d photos: RESOURCE_SNAP_VERSION changed.\n", eaSize( &state->eaResources ));
	} else if( file->skyFileTimestamp != skyFileTimestamp ) {
		printf( "Taking all %d photos: Skyfile changed; Old TS=%d, New TS=%d\n",
				eaSize( &state->eaResources ), file->skyFileTimestamp, skyFileTimestamp );
	} else {
		int it;
		loadstart_printf( "Checking timestamps..." );
		for( it = eaSize( &state->eaResources ) - 1; it >= 0; --it ) {
			ResourceSnapDesc* resource = state->eaResources[ it ];
			int ts = gclSnapGetResourceTimestamp( state, resource );
			int prevTs;
			char path[ MAX_PATH ];

			sprintf( path, "bin/images/%s.wtex", gclSnapGetResourceString( resource, true ));
			prevTs = fileLastChanged( path );

			if( prevTs > ts ) {
				binNotifyTouchedOutputFile( path );
				eaRemove( &state->eaResources, it );
			} else {
				verbose_printf( "Taking photo %s: Old TS=%d, New TS=%d\n",
								gclSnapGetResourceString( resource, false ), prevTs, ts );
			}
		}
		loadend_printf( " done. Taking %d photos.", eaSize( &state->eaResources ));
	}
}

static int gclSnapGetGroupDefTimestamp( ResourceSnapState* state, GroupDef* def )
{
	int ts;
	char buffer[ RESOURCE_NAME_MAX_SIZE ];
	sprintf( buffer, "ObjectLibrary__%d", def->name_uid );
	
	if( stashFindInt( state->timestamps, buffer, &ts )) {
		return ts;
	}

	ts = fileLastChanged( def->filename );

	FOR_EACH_IN_EARRAY( def->children, GroupChild, child ) {
		int child_ts;
		GroupDef *child_def = objectLibraryGetGroupDef(child->name_uid, false);
		if( child_def ) {
			child_ts = gclSnapGetGroupDefTimestamp( state, child_def );
			ts = MAX( ts, child_ts );
		}
	} FOR_EACH_END;

	stashAddInt( state->timestamps, buffer, ts, true );
	return ts;
}

static int gclSnapGetCGeoTimestamp( ResourceSnapState* state, PCGeometryDef* cgeo )
{
	if( cgeo ) {
		int ts;
		char buffer[ RESOURCE_NAME_MAX_SIZE ];
		sprintf( buffer, "CostumeGeometry__%s", cgeo->pcName );

		if( stashFindInt( state->timestamps, buffer, &ts )) {
			return ts;
		}

		ts = fileLastChanged( cgeo->pcFileName );

		if( cgeo->pcModel ) {
			ModelHeader* model = wlModelHeaderFromName( cgeo->pcModel );
			int modelTs = fileLastChanged( model->filename );

			ts = MAX( ts, modelTs );
		}

		stashAddInt( state->timestamps, buffer, ts, true );
		return ts;
	}
	
	return 0;
}

static int gclSnapGetCMatTimestamp( ResourceSnapState* state, PCMaterialDef* cmat )
{
	if( cmat ) {
		int ts;
		char buffer[ RESOURCE_NAME_MAX_SIZE ];
		sprintf( buffer, "CostumeMaterial__%s", cmat->pcName );

		if( stashFindInt( state->timestamps, buffer, &ts )) {
			return ts;
		}

		ts = fileLastChanged( cmat->pcFileName );

		stashAddInt( state->timestamps, buffer, ts, true );
		return ts;
	}

	return 0;
}

static int gclSnapGetCTexTimestamp( ResourceSnapState* state, PCTextureDef* ctex )
{
	if( ctex ) {
		int ts;
		char buffer[ RESOURCE_NAME_MAX_SIZE ];
		sprintf( buffer, "CostumeTexture__%s", ctex->pcName );

		if( stashFindInt( state->timestamps, buffer, &ts )) {
			return ts;
		}

		ts = fileLastChanged( ctex->pcFileName );
		{
			BasicTexture* tex = texFind( ctex->pcNewTexture, false );
			int texTs = fileLastChanged( tex->fullname );
			ts = MAX( ts, texTs );
		}
		{
			int it;
			for( it = 0; it != eaSize( &ctex->eaExtraSwaps ); ++it ) {
				BasicTexture* tex = texFind( ctex->eaExtraSwaps[ it ]->pcNewTexture, false );
				int texTs = fileLastChanged( tex->fullname );
				ts = MAX( ts, texTs );
			}
		}

		stashAddInt( state->timestamps, buffer, ts, true );
		return ts;
	}

	return 0;
}

static int gclSnapGetPlayerCostumeTimestamp( ResourceSnapState* state, PlayerCostume* costume )
{
	int ts;
	char buffer[ RESOURCE_NAME_MAX_SIZE ];

	// Only in -makebinsandexit can we rely on the whole dictionary
	// being loaded on the client.  If the costume is not loaded in
	// this case, we want to return an error state.
	if( !gbMakeBinsAndExit && !costume ) {
		return -1;
	}
	
	sprintf( buffer, "PlayerCostume__%s", costume->pcName );

	if( stashFindInt( state->timestamps, buffer, &ts )) {
		return ts;
	}
	
	ts = fileLastChanged( costume->pcFileName );

	// look at all parts
	FOR_EACH_IN_EARRAY( costume->eaParts, PCPart, part ) {
		int cgeoTs = gclSnapGetCGeoTimestamp( state, GET_REF( part->hGeoDef ));
		int cmatTs = gclSnapGetCMatTimestamp( state, GET_REF( part->hMatDef ));
		int patternTs = gclSnapGetCTexTimestamp( state, GET_REF( part->hPatternTexture ));
		int detailTs = gclSnapGetCTexTimestamp( state, GET_REF( part->hDetailTexture ));
		int specularTs = gclSnapGetCTexTimestamp( state, GET_REF( part->hSpecularTexture ));
		int diffuseTs = gclSnapGetCTexTimestamp( state, GET_REF( part->hDiffuseTexture ));

		ts = MAX( ts, cgeoTs );
		ts = MAX( ts, cmatTs );
		ts = MAX( ts, patternTs );
		ts = MAX( ts, detailTs );
		ts = MAX( ts, specularTs );
		ts = MAX( ts, diffuseTs );
	} FOR_EACH_END;

	stashAddInt( state->timestamps, buffer, ts, true );
	return ts;
}

int gclSnapGetResourceTimestamp( ResourceSnapState* state, const ResourceSnapDesc* resource )
{
	if( stricmp( resource->astrDictName, "ObjectLibrary" ) == 0 ) {
		return gclSnapGetGroupDefTimestamp( state, objectLibraryGetGroupDefByName( resource->astrResName, false ));
	} else if( stricmp( resource->astrDictName, "PlayerCostume" ) == 0 ) {
		return gclSnapGetPlayerCostumeTimestamp( state, RefSystem_ReferentFromString( "PlayerCostume", resource->astrResName ));
	} else {
		return -1;
	}
}

int gclSnapGetSkyFileTimestamp( void )
{
	SkyInfo* skyFile = gfxSkyFindSky( PREVIEW_SKY );
	assertmsgf( skyFile, "The ResourceSnap system is missing its PREVIEW_SKY.  This sky is necessary for the ResourceSnap system to take controlled photos.  Did it get deleted?" );

	return fileLastChanged( skyFile->filename );
}

const char* gclSnapGetResourceString( const ResourceSnapDesc* desc, bool isForTexName )
{
	static char buffer[ 256 ];
	
	if( isForTexName ) {
		if( stricmp( desc->astrDictName, "ObjectLibrary" ) == 0 ) {
			sprintf( buffer, "dyn_objlib_%s_%s",
					 desc->astrResName,
					 (desc->objectIsTopDownView ? "2D" : "3D") );
		} else {
			sprintf( buffer, "dyn_%s_%s", desc->astrDictName, desc->astrResName );
		}
	} else {
		if( stricmp( desc->astrDictName, "ObjectLibrary" ) == 0 ) {
			GroupDef* def = objectLibraryGetGroupDefByName( desc->astrResName, false );
			if( def ) {
				sprintf( buffer, "GroupDef %s (%d), %s",
						 def->name_str, def->name_uid,
						 (desc->objectIsTopDownView ? "2D" : "3D") );
			} else {
				sprintf( buffer, "GroupDef (null)" );
			}
		} else if( stricmp( desc->astrDictName, "PlayerCostume" ) == 0 ) {
			sprintf( buffer, "PlayerCostume %s", desc->astrResName );
		} else {
			sprintf( buffer, "Unsupported %s %s", desc->astrDictName, desc->astrResName );
		}
	}

	return buffer;
}

#include "gclResourceSnap_h_ast.c"
#include "gclResourceSnap_c_ast.c"
