#include "GfxHeadshot.h"

#include "GfxAuxDevice.h"
#include "GfxDebug.h"
#include "GfxHeadshot.h"
#include "GfxLightCache.h"
#include "GfxMapSnap.h"
#include "GfxMaterials.h"
#include "GfxPostprocess.h"
#include "GfxSky.h"
#include "GfxSurface.h"
#include "GfxTextures.h"
#include "GfxWorld.h"
#include "GfxDrawFrame.h"
#include "GraphicsLibPrivate.h"
#include "RdrTexture.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "WritePNG.h"
#include "dynDraw.h"
#include "dynFxInterface.h"
#include "dynFxManager.h"
#include "dynSkeleton.h"
#include "jpeg.h"
#include "partition_enums.h"
#include "rand.h"
#include "tga.h"
#include "wlCostume.h"
#include "dynFxInfo.h"

GCC_SYSTEM

AUTO_RUN_ANON(memBudgetAddMapping( __FILE__, BUDGET_EngineMisc ););

#define HEADSHOT_SKY_NAME "headshot_sky"
#define HEADSHOT_CUBEMAP_NAME "headshot_cubemap_cube"
#define HEADSHOT_TEXTURE_CLEANUP_WAIT_FRAMES 3
#define HEADSHOT_VISSCALE (5.0f*5.0f)

typedef struct HeadshotInfo HeadshotInfo;

SA_RET_OP_VALID static HeadshotInfo* gfxHeadshotInfoCostumeCreate(
		SA_PARAM_NN_VALID BasicTexture* texture,
		SA_PARAM_NN_VALID WLCostume* costume,
		SA_PARAM_NN_VALID BasicTexture* background,
		SA_PARAM_OP_VALID const char* framename,
		SA_PARAM_OP_VALID Color bgColor,
		bool forceBodyshot,
		SA_PARAM_OP_VALID DynBitFieldGroup* bfg,
		SA_PARAM_OP_STR const char *pcAnimKeyword,
		SA_PARAM_OP_STR const char *pchStance,
		SA_PARAM_OP_STR const char* sky,
		SA_PARAM_OP_VALID Vec3 camPos,
		SA_PARAM_OP_VALID Vec3 camDir,
		float animDelta,
		char **ppExtraInfo,
		float fovY,
		SA_PARAM_OP_VALID HeadshotUpdateCameraF updateCameraF,
		SA_PARAM_OP_VALID UserData updateCameraData,
		PCFXTemp ***peaExtraFX
		MEM_DBG_PARMS );
static void gfxHeadshotInfoDestroy( SA_PARAM_NN_VALID HeadshotInfo* info );
static void gfxHeadshotInfoDestroyEx( SA_PARAM_NN_VALID HeadshotInfo* info, bool bFreeTexture, bool bRemoveActive );
static void gfxHeadshotUpdateExisting(	HeadshotInfo* info, 
										BasicTexture* texture, 
										BasicTexture* background, 
										const char* framename, 
										Color bgColor, 
										bool forceBodyshot, 
										DynBitFieldGroup* bfg, 
										const char* pcAnimBits,
										const char* sky, 
										Vec3 camPos, 
										Vec3 camDir, 
										float animDelta, 
										char **ppExtraInfo);

static void headshotCaptureSkeleton( const Vec3 camPos, const Vec3 camDir, HeadshotInfo* info );
static void gfxHeadshotSkeletonCalcCamPosAndDir( Vec3 outCamPos, Vec3 outCamDir, SA_PARAM_NN_VALID HeadshotInfo* info );
static void gfxHeadshotCalcCamPosAndDir(
		Vec3 outCamPos, Vec3 outCamDir, const Vec3 min, const Vec3 max,
		float aspectRatio, const Vec3 pyr, float fovY );
static void gfxHeadshotCostumeDictChanged(
		enumResourceEventType eType, const char *pDictName, const char *pRefData,
		Referent pReferent, void *pUserData );

static void gfxHeadshotRecreate( HeadshotInfo** info );
static void gfxHeadshotCapture1( HeadshotInfo* info, bool reopenTempEntry );
static void gfxHeadshotCaptureCostume1( HeadshotInfo* info );
static void gfxHeadshotCaptureGroup1( HeadshotInfo* info, bool reopenTempEntry );
static void gfxHeadshotCaptureScene1( SA_PARAM_NN_VALID HeadshotInfo* info );
static void gfxHeadshotCaptureConstructedScene1( SA_PARAM_NN_VALID HeadshotInfo* info );
static void gfxHeadshotCaptureModel1( SA_PARAM_NN_VALID HeadshotInfo* info );
static void gfxHeadshotValidateSizeForRuntime( int* width, int* height );
static void gfxHeadshotValidateSizeForHeadshotServer( int* width, int* height );

static BasicTexture* gfxHeadshotAllocateTexture( const char* texName, int width, int height );
static void gfxHeadshotSwapWithCompatibleTexture( BasicTexture* tex );
#define texAllocateScratch( texName, width, height, useFlags ) call_gfxHeadshotAllocateTexture_instead

/// If true (right now should only be on for MapSnap) do not validate
/// the size of a headshot to the list of sizes.
bool gfxHeadshotDisableValidateSize = false;

/// Number of frames to wait before headshot loading is considered
/// finished.
///
/// MapSnap needs this set to 4 or else map pieces end up not getting
/// loaded.  Every other headshot seems fine with 2.  This is externed
/// to allow MapSnap to globally override it.
int gfxHeadshotFramesToWait = 2;

/// If true, render all headshots in debug mode
static bool gfxHeadshotDebug = false;
AUTO_CMD_INT( gfxHeadshotDebug, gfxHeadshotDebug );

bool gfxGetHeadshotDebug()
{
	return gfxHeadshotDebug;
}

/// If true, render active headshots as debug thumbnails
static bool gfxHeadshotDebugThumbnail = false;
AUTO_CMD_INT( gfxHeadshotDebugThumbnail, gfxHeadshotDebugThumbnail );

/// If true, never render headshots
static bool gfxHeadshotDisable = false;
AUTO_CMD_INT( gfxHeadshotDisable, gfxHeadshotDisable );

/// If true, render alpha headshots in debug mode
static bool gfxHeadshotAlphaDebug = false;
AUTO_CMD_INT( gfxHeadshotAlphaDebug, gfxHeadshotAlphaDebug );

/// If true, retake images that are suspected of being bad
bool gfxHeadshotDoBadImageChecks = false;
AUTO_CMD_INT( gfxHeadshotDoBadImageChecks, gfxHeadshotDoBadImageChecks );

/// A way to override the camera position / direction
Vec3 gfxHeadshotOverrideCamPos;
Vec3 gfxHeadshotOverrideCamDir;
AUTO_COMMAND;
void gfxHeadshotOverrideCamPosAndDir( float posX, float posY, float posZ, float dirX, float dirY, float dirZ )
{
	gfxHeadshotOverrideCamPos[ 0 ] = posX;
	gfxHeadshotOverrideCamPos[ 1 ] = posY;
	gfxHeadshotOverrideCamPos[ 2 ] = posZ;
	gfxHeadshotOverrideCamDir[ 0 ] = dirX;
	gfxHeadshotOverrideCamDir[ 1 ] = dirY;
	gfxHeadshotOverrideCamDir[ 2 ] = dirZ;
}

/// If true, we allow indoor lights and sun lights at the same time on the action
bool gfxHeadshotUseSunIndoors = false;

/// If true, we are taking ResourceSnap photos.  This changes the
/// video mem backing's size
bool gfxHeadshotIsTakingResourceSnapPhotos = false;

/// If true, time does not progress for animated headshots.
bool gfxHeadshotAnimationIsPaused = false;

/// If true, this is in HeadshotServer mode.
static bool gfxHeadshotIsHeadshotServer = false;
bool gfxIsHeadshotServer(void)
{
	return gfxHeadshotIsHeadshotServer;
}

/// All the information needed to process one headshot action.
typedef struct HeadshotAction {
	HeadshotInfo* info;
} HeadshotAction;

/// All the information needed to process one alpha recovery pass.
typedef struct HeadshotAlphaAction {
	BasicTexture* srcBlackTex;
	BasicTexture* srcGrayTex;
	HeadshotInfo* info;
} HeadshotAlphaAction;

typedef enum HeadshotType {
	HETY_COSTUME,
	HETY_ANIMATED_COSTUME,
	HETY_GROUP,
	HETY_ANIMATED_GROUP,
	HETY_SCENE,
	HETY_CONSTRUCTED_SCENE,
	HETY_MODEL,
	HETY_MATERIAL,
} HeadshotType;

/// All relevant information for generating a headshot.
typedef struct HeadshotInfo {
	// Debug data:
	const char* fname;
	int line;
	
	BasicTexture* texture;
	int regionId;
	float fovY;

	HeadshotNotifyF notifyF;
	UserData notifyData;
	HeadshotNotifyBytesF notifyBytesF;
	UserData notifyBytesData;
	HeadshotUpdateCameraF updateCameraF;
	UserData updateCameraData;

	bool needsRecreate;
	bool bForUI;

	HeadshotType type;
	union {
		struct {
			const char* costumeName;
			DynNode* root;
			DynFxManager* fxMan;
			DynSkeleton* playerSkel;
			DynDrawSkeleton* playerDrawSkel;
			DynBitFieldGroup bfg;
			const char *pcAnimKeyword;
			const char *pchStance;
			Vec3 overrideCamPos;
			Vec3 overrideCamDir;
			const char* framename;
			float animDelta;
			PCFXTemp **eaFX;
			
			unsigned forceBodyshot : 1;
			unsigned useOverrideCamera : 1;
		} costume;

		struct {
			int uid;
			GfxHeadshotObjectCamType camType;
			GroupChildParameter **params;
			F32 nearPlane;
			bool enableShadows;
			bool showHeadshotGroups;
		} group;

		struct {
			Vec3 camPos;
			Vec3 camDir;
		} scene;

		struct {
			GfxCameraController *camController;
			bool allowOutlines;
		} constructedScene;

		struct {
			Material* material;
			Model* model;
			BasicTexture **eaTextureSwaps;
		} model;
	};
	
	BasicTexture* background;
	Vec4 bgTexCoords;
	Color bgColor;
	char* sky;
	int badCount;
	
	// extra info to be stored in the jpg file
	char *esExtraInfo;

} HeadshotInfo;

typedef struct HeadshotOrthoParams
{
	F32 world_w;
	F32 world_h;
	F32 far_plane;
	F32 near_plane;
	Vec3 center;
	Vec3 pyr;
} HeadshotOrthoParams;

/// Animated headshot info
static HeadshotInfo** headshotAnimatedList = NULL;

/// The animated skeleton's camera position
static Vec3 headshotAnimatedCamPos = { 0, 0, 0 };

/// The animated skeleton's camera position
static Vec3 headshotAnimatedCamDir = { 0, 0, 1 };

/// A bunch of cached camera views.  You need one for each headshot
/// action.
static GfxCameraView** headshotCameraViewList = NULL;

/// A bunch of cached camera controllers.  You need one for each
/// headshot action.
static GfxCameraController** headshotCameraControllerList = NULL;

/// A bunch of ambient light data.  You need one for ecah headshot
/// action.
static RdrAmbientLight** headshotAmbientLightList = NULL;

/// Sun light data for the headshots.
static GfxLight *headshotSunLight = NULL;
static GfxLight *headshotSunLight2 = NULL;
static GfxLight *realRegionSunLight = NULL;
static GfxLight *realRegionSunLight2 = NULL;

/// The list of headshot actions to be processed this frame.
static HeadshotAction** headshotActionsThisFrame = NULL;

/// The list of alpha recovery actions to be processed this frame.
static HeadshotAlphaAction** headshotAlphaActionsThisFrame = NULL;

/// All the headshots that are currently in use.
///
/// This is needed so that when the device is lost, the headshots can
/// be regenerated.
static HeadshotInfo** headshotActiveList = NULL;

/// All the headshots that still need to be processed.
static HeadshotInfo** headshotUnprocessedList = NULL;

/// All the headshots that still need alpha to be processed.
static HeadshotInfo** headshotAlphaUnprocessedList = NULL;

// Currently for cleaning up headshot textures used by the UI
static BasicTexture** headshotTextureCleanupList = NULL;
static const char** headshotTextureCleanupListFname = NULL;
static int* headshotTextureCleanupListLinenum = NULL;

/// All free regions for headshots
static int* headshotFreeRegionIds = NULL;

/// How many regions have been created so far
static int headshotRegionCounter = 0;

/// If any headshots were requested this frame.
static int headshotRequestedFramesAgo;

/// How many frames to wait before cleaning up all textures in headshotTextureCleanupList
static int headshotFramesUntilTextureCleanup = 0;

/// If headshots were recreated this frame
static int headshotRecreatedFramesAgo;

/// How many frames loading has been completed form
static int headshotLoadingFinishedFramesAgo;

/// Previously freed headshot textures.
///
/// This helps reduce render target allocation/freeing which NVidia
/// told us was bad.
static BasicTexture** headshotTexturePool = NULL;

/// Temporary surface for headshots that don't fit on the primary surface
static GfxTempSurface* headshotScratchTempSurface = NULL;

/// If set, use this color instead of the info's BG-COLOR.
static Color* headshotOverrideBGColor;

/// If set, use this texture instead of the info's TEXTURE.
static BasicTexture* headshotOverrideDestTexture;

/// The maximum number of headshots to take in a single frame
static int headshotMaxPerFrameInternal = 4;
AUTO_CMD_INT( headshotMaxPerFrameInternal, headshotMaxPerFrame ) ACMD_CMDLINE ACMD_CALLBACK(headshotMaxPerFrameValidate);

void headshotMaxPerFrameValidate(void)
{
	assert(headshotMaxPerFrameInternal >= HEADSHOT_MAX_ANIMATED + 1);
}

// Map Snaps assume that the textures will get returned sequentially,
// and that no surface resizing will happen.
static int headshotMaxPerFrame(void)
{
	if( gfxIsTakingMapPhotos() ) {
		return 1;
	} else {
		return headshotMaxPerFrameInternal;
	}
}

static DynBitFieldGroup headshotDefaultBFG;
static bool headshotDefaultBFGSet = false;

static void gfxHeadshotAssertConsistantState( void )
{
	// If we encounter weird headshot crashes, then comment this in to
	// help track it down.
	/*
	int it;
	for( it = 0; it != eaSize( &headshotActionsThisFrame ); ++it ) {
		HeadshotInfo* info = headshotActionsThisFrame[ it ]->info;

		assert( info );
		if( info->type != HETY_ANIMATED_COSTUME && info->type != HETY_ANIMATED_GROUP ) {
			assert( eaFind( &headshotActiveList, info ) != -1 );
		}
	}
	for( it = 0; it != eaSize( &headshotAnimatedList ); ++it ) {
		HeadshotInfo* info = headshotAnimatedList[ it ];

		assert( info->type == HETY_ANIMATED_COSTUME || info->type == HETY_ANIMATED_GROUP );
	}
	*/
}

static bool gfxHeadshotFreeTexture(BasicTexture* pTexture)
{
	if (pTexture && (pTexture->flags & TEX_SCRATCH)) {
		eaPush(&headshotTexturePool, pTexture);
		return true;
	}
	return false;
}

/// Initialize headshot data
static void gfxHeadshotInit( void )
{
	// need to save room for an animated headshot + the max per frame
	int maxCamerasNeeded = RoundUpToGranularity( headshotMaxPerFrame(), 2 );
	
	while( eaSize( &headshotCameraViewList ) < maxCamerasNeeded ) {
		GfxCameraView* accum = calloc( 1, sizeof( *accum ));
		gfxInitCameraView( accum );

		eaPush( &headshotCameraViewList, accum );
	}
	while( eaSize( &headshotCameraControllerList ) < maxCamerasNeeded ) {
		GfxCameraController* accum = calloc( 1, sizeof( GfxCameraController ));
		gfxInitCameraController( accum, gfxNullCamFunc, NULL );
		accum->override_time = true;
		accum->time_override = 0;

		eaPush( &headshotCameraControllerList, accum );
	}
	while( eaSize( &headshotAmbientLightList ) < maxCamerasNeeded ) {
		RdrAmbientLight* accum = calloc( 1, sizeof( *accum ));
		eaPush( &headshotAmbientLightList, accum );
	}

	// Make sure costume changes are known about
	{
		static bool resDictCallbacksRegistered = false;
		if( !resDictCallbacksRegistered ) {
			resDictRegisterEventCallback( "Costume", gfxHeadshotCostumeDictChanged, NULL );
			resDictCallbacksRegistered = true;
		}
	}

	// Make sure that the default bfg is set
	if( !headshotDefaultBFGSet ) { 
		dynBitFieldGroupAddBits( &headshotDefaultBFG, "HEADSHOT IDLE NOLOD", true );
		headshotDefaultBFGSet = true;
	}
}

/// Check if INFO wants an alpha pass.
static bool gfxHeadshotInfoWantsAlpha( const HeadshotInfo* info )
{
	return info->bgColor.a < 255 && info->type != HETY_ANIMATED_COSTUME && info->type != HETY_ANIMATED_GROUP;
}

/// Get the headshot's temp scratch surface
RdrSurface* gfxHeadshotScratchSurface( const HeadshotInfo* info )
{
	RdrSurface * headshotScratchSurface = NULL;
	int desiredWidth;
	int desiredHeight;

	if( gfxIsTakingMapPhotos() ) {
		desiredWidth = info->texture->width;
		desiredHeight = info->texture->height;
	} else if( gfxHeadshotIsHeadshotServer || gfxHeadshotIsTakingResourceSnapPhotos ) {
		desiredWidth = 1024;
		desiredHeight = 1024;
	} else {
		int widthHeight[2];
		gfxGetRenderSizeFromScreenSize(widthHeight);
		desiredWidth = widthHeight[0];
		desiredHeight = widthHeight[1];
	}

	if( !headshotScratchSurface || headshotScratchSurface->width_nonthread < desiredWidth || headshotScratchSurface->height_nonthread < desiredHeight ) {
		RdrSurfaceParams surfaceParams = {0};

		surfaceParams.name = "Headshot Scratch";
		rdrSurfaceParamSetSizeSafe(&surfaceParams, desiredWidth, desiredHeight);
		surfaceParams.desired_multisample_level = 1;
		surfaceParams.required_multisample_level = 1;
		surfaceParams.depth_bits = 24;
		surfaceParams.flags = 0;
		surfaceParams.buffer_types[0] = SBT_RGBA;

		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceParams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams( &surfaceParams );

		gfxReleaseTempSurface(headshotScratchTempSurface);
		headshotScratchTempSurface = gfxGetTempSurface( &surfaceParams );
		headshotScratchSurface = headshotScratchTempSurface->surface;
	}

	return headshotScratchSurface;
}

/// Get a headshot info's viewport to use.
///
/// Format of the viewport is { W, H, X, Y }, in percentage.
static void gfxHeadshotInfoViewport( Vec4 outVec, const HeadshotInfo* info )
{
	RdrSurface* scratchSurface = gfxHeadshotScratchSurface( info );
	BasicTexture* texture = info->texture;
	
	setVec4( outVec,
			 MIN( 1.0, (float)texture->width / scratchSurface->width_nonthread ),
			 MIN( 1.0, (float)texture->height / scratchSurface->height_nonthread ),
			 0,
			 0 );
}

static int gfxCountNumberHeadshotActions( void )
{
	int count = 0;
	FOR_EACH_IN_EARRAY(gfx_state.currentDevice->actions, GfxRenderAction, action)
	{
		if (action->action_type == GfxAction_Headshot)
			count++;
	}
	FOR_EACH_END;

	return count;
}

/// This function should be called once per frame, when any headshots
/// could have been requested.
void gfxHeadshotOncePerFrame( void )
{
	PERFINFO_AUTO_START_FUNC();

	++headshotRequestedFramesAgo;
	++headshotRecreatedFramesAgo;

	if (headshotFramesUntilTextureCleanup > 0)
	{
		if (--headshotFramesUntilTextureCleanup == 0)
		{
			//There shouldn't be any textures left in the cleanup list at this point. Error if any are found.
			int i;
			for (i = eaSize(&headshotTextureCleanupList)-1; i >= 0; i--)
			{
				BasicTexture* pTexture = headshotTextureCleanupList[i];
				const char* fname = headshotTextureCleanupListFname[i];
				int line = headshotTextureCleanupListLinenum[i];

				ErrorDetailsf("%s(%d): Headshot '%s' freed automatically",
							  fname, line, pTexture->name);
				Errorf("gfxHeadshotReleaseAll was requested, but headshot was not freed shortly after. Freeing automatically to prevent leaks.");
				gfxHeadshotFreeTexture(pTexture);
				eaRemoveFast(&headshotTextureCleanupList, i);
				eaRemoveFast(&headshotTextureCleanupListFname, i);
				eaiRemoveFast(&headshotTextureCleanupListLinenum, i);
			}
		}
	}
	if (headshotScratchTempSurface)
		gfxMarkTempSurfaceUsed(headshotScratchTempSurface);

	gfxUFHeadshotCleanup();

	// Move any out of date headshots back to the unprocessed list.
	{
		int it;
		for( it = 0; it != eaSize( &headshotActiveList ); ++it ) {
			HeadshotInfo** headshotInfo = &headshotActiveList[ it ];
			if( (*headshotInfo)->needsRecreate ) {
				gfxHeadshotRecreate( headshotInfo );
				headshotRecreatedFramesAgo = 0;
			}
		}
		for( it = 0; it != eaSize( &headshotAnimatedList ); ++it ) {
			HeadshotInfo** headshotInfo = &headshotAnimatedList[ it ];
			if( (*headshotInfo)->needsRecreate ) {
				gfxHeadshotRecreate( headshotInfo );
				headshotRecreatedFramesAgo = 0;
			}
		}
	}

	devassert( gfxCountNumberHeadshotActions() == 0 );

	{
		int numHeadshotsAnimatedUnprocessed = eaSize( &headshotAnimatedList );
		int numHeadshotsAlphaUnprocessed = eaSize( &headshotAlphaUnprocessedList );
		int numHeadshotsUnprocessed = eaSize( &headshotUnprocessedList );
		int actionIt = 0;
		int animatedIt;
		int it;

		// There are a limited number of headshot photos allowed per frame so 
		// we need to prevent animated headshots (which never finish because 
		// they are ongoing) from crowding out mapsnap photo, causing mapsnap 
		// to never complete.
		if (!gfxIsTakingMapPhotos())
		{
			for( animatedIt = 0; animatedIt < numHeadshotsAnimatedUnprocessed && actionIt < headshotMaxPerFrame(); ++animatedIt, ++actionIt ) {
				HeadshotInfo* headshot;

				headshot = headshotAnimatedList[ animatedIt ];

				gfxHeadshotCapture1( headshot, true );
			}
		}

		for( it = 0; it < numHeadshotsAlphaUnprocessed && actionIt < headshotMaxPerFrame() - 1; ++it, actionIt += 2 ) {
			HeadshotInfo* headshot; 
			int actionStart = eaSize( &headshotActionsThisFrame );
			BasicTexture* blackBgTex;
			BasicTexture* grayBgTex;

			if( gfxHeadshotIsHeadshotServer ) {
				headshot = headshotAlphaUnprocessedList[ it ];
			} else {
				headshot = headshotAlphaUnprocessedList[ numHeadshotsAlphaUnprocessed - it - 1 ];
			}

			if( !gfxStartActionQuery( GfxAction_Headshot, 2 )) {
				break;
			}

			blackBgTex = gfxHeadshotAllocateTexture( "HeadshotAlpha_Black", headshot->texture->width, headshot->texture->height );
			grayBgTex = gfxHeadshotAllocateTexture( "HeadshotAlpha_Gray", headshot->texture->width, headshot->texture->height );

			{
				int origBloomQuality = gfx_state.settings.bloomQuality;
				gfx_state.settings.bloomQuality = GBLOOM_OFF;
				headshotOverrideBGColor = &ColorBlack;
				headshotOverrideDestTexture = blackBgTex;
				gfxHeadshotCapture1( headshot, true );
				
				headshotOverrideBGColor = &ColorGray;
				headshotOverrideDestTexture = grayBgTex;
				gfxHeadshotCapture1( headshot, false );
				
				headshotOverrideBGColor = NULL;
				headshotOverrideDestTexture = NULL;

				gfx_state.settings.bloomQuality = origBloomQuality;
			}

			assert( eaSize( &headshotActionsThisFrame ) == actionStart + 2 );
			{
				HeadshotAction* blackBGAction = headshotActionsThisFrame[ actionStart + 0 ];
				HeadshotAction* grayBGAction = headshotActionsThisFrame[ actionStart + 1 ];
				HeadshotAlphaAction* accum = calloc( 1, sizeof( *accum ));

				eaSetSize( &headshotActionsThisFrame, actionStart );
				accum->srcBlackTex = blackBgTex;
				accum->srcGrayTex = grayBgTex;
				accum->info = headshot;
				eaPush( &headshotAlphaActionsThisFrame, accum );

				free( blackBGAction );
				free( grayBGAction );
			}
		}
	
		for( it = 0; it < numHeadshotsUnprocessed && actionIt < headshotMaxPerFrame(); ++it, ++actionIt ) {
			HeadshotInfo* headshot;

			if( gfxHeadshotIsHeadshotServer ) {
				headshot = headshotUnprocessedList[ it ];
			} else {
				headshot = headshotUnprocessedList[ numHeadshotsUnprocessed - it - 1 ];
			}
		
			gfxHeadshotCapture1( headshot, true );
		}
	}

	PERFINFO_AUTO_STOP();

	gfxHeadshotAssertConsistantState();
}

/// This function should be called once per frame, when any headshots
/// could have been requested.
void gfxHeadshotOncePerFrameMinimal( void )
{
	gfxUFHeadshotCleanup();
	
	gfxHeadshotAssertConsistantState();
}

/// A notify bytes function useful for saving the data out to disk. 
void gfxHeadshotNotifyBytesSaveFile( char* destFileRaw, U8* data, int width, int height, char **ppExtraInfo )
{
	char destFile[ MAX_PATH ];

	PERFINFO_AUTO_START_FUNC();
	
	if( fileIsAbsolutePath( destFileRaw )) {
		strcpy( destFile, destFileRaw );
	} else {
		fileSpecialDir( "screenshots", SAFESTR( destFile ));
		strcat( destFile, "/" );
		strcat( destFile, destFileRaw );
	}
	
	// Reformat bytes into BGRA mode.
	{
		int byteIt;
		for( byteIt = 0; byteIt != width * height; ++byteIt ) {
			U8 r = data[ byteIt * 4 + 0 ];
			U8 g = data[ byteIt * 4 + 1 ];
			U8 b = data[ byteIt * 4 + 2 ];
			U8 a = data[ byteIt * 4 + 3 ];

			data[ byteIt * 4 + 0 ] = b;
			data[ byteIt * 4 + 1 ] = g;
			data[ byteIt * 4 + 2 ] = r;
			data[ byteIt * 4 + 3 ] = a;
		}
	}

	if( strEndsWith( destFile, ".jpg" ) || strEndsWith( destFile, ".jpeg" ))
	{
		if(ppExtraInfo && *ppExtraInfo)
		{
			jpgSaveEx(destFile, data, 4, width, height, *ppExtraInfo, estrLength(ppExtraInfo), 95);
		}
		else
		{
			jpgSave( destFile, data, 4, width, height, 95 );
		}
		
	}
	else if(strEndsWith(destFile, ".png"))
	{
		WritePNG_File(data, width, height, width, 4, destFile);
	}
	else
	{
		changeFileExt( destFile, ".tga", destFile );
		tgaSave( destFile, data, width, height, 4 );
	}

	free( destFileRaw );
	PERFINFO_AUTO_STOP();
}

/// Does a check to see if any images are bad.
/// Bad is currently defined as mostly black.
/// This is a very slow action.
bool gfxHeadshotHasBadImage( void )
{
	bool badImageExists = false;
	int it;
	for( it = 0; it != eaSize( &headshotActionsThisFrame ); ++it ) {
		HeadshotAction* action = headshotActionsThisFrame[ it ];
		HeadshotInfo* info = action->info;

		if( info && info->badCount < 2 ) {
			int w;
			int h;
			U8* data = rdrGetTextureData( gfx_state.currentDevice->rdr_device,
										  info->texture->tex_handle,
										  RTEX_2D, RTEX_BGRA_U8, &w, &h );
			int blackCount = 0;
			int x, y;

			devassert( w == info->texture->width && h == info->texture->height );
			for ( y=0; y < h; y++ ) {
				for ( x=0; x < w; x++ ) {
					if(   data[(x+y*w)*4 + 0] < 50 
						  && data[(x+y*w)*4 + 1] < 50 
						  && data[(x+y*w)*4 + 2] < 50 ) {
						blackCount++;
					}
				}
			}
			free( data );
			if(blackCount > (w*h)/2){
				info->badCount++;
				badImageExists = true;
				break;
			}
		}
	}
	return badImageExists;
}

/// This function should be called once after any headshots could have
/// been done.
void gfxUFHeadshotPostRendering( void )
{
	// Loading takes at least one frame before it registers correctly
	bool stillLoading = gfxIsStillLoading(true);
	bool isHeadshotsFinished = (!stillLoading
								&& headshotRequestedFramesAgo > gfxHeadshotFramesToWait
								&& headshotLoadingFinishedFramesAgo > gfxHeadshotFramesToWait
								&& !gfxHeadshotDebug);

	if(isHeadshotsFinished && gfxHeadshotDoBadImageChecks && gfxHeadshotHasBadImage()) {
		texFreeAllNonUI();
		stillLoading = true;
		isHeadshotsFinished = false;
	}

	if( !stillLoading ) {
		++headshotLoadingFinishedFramesAgo;
	} else {
		headshotLoadingFinishedFramesAgo = 0;
	}

	assert( eaSize( &headshotActionsThisFrame ) + eaSize( &headshotAlphaActionsThisFrame ) * 2
			<= RoundUpToGranularity( headshotMaxPerFrame(), 2 ) );
	
	// HeadshotAlphaAction pass
	{

		int it;
		for( it = 0; it != eaSize( &headshotAlphaActionsThisFrame ); ++it ) {
			const HeadshotAlphaAction* action = headshotAlphaActionsThisFrame[ it ];
			const HeadshotInfo* info = action->info;
			BasicTexture* texture = info->texture;
			RdrSurface* scratchSurface = gfxHeadshotScratchSurface( info );
			Vec4 viewport;
			gfxHeadshotInfoViewport( viewport, info );
			
			gfxBeginSection( "Headshot Recover Alpha" );

			// Update the viewport since this happens OUTSIDE of a render action.
			rdrSurfaceUpdateMatrices( scratchSurface, unitmat44, unitmat44, unitmat44, unitmat, unitmat, unitmat, 0.1, 1, 0.95,
									  viewport[2], viewport[0], viewport[3], viewport[1],
									  zerovec3 );
			
			gfxSetActiveSurface( scratchSurface );
			gfxDoRecoverAlphaForHeadshot( scratchSurface, action->srcBlackTex->tex_handle, action->srcGrayTex->tex_handle, info->bgColor );

			if( texture->flags & TEX_SCRATCH ) {
				U32 imageByteCount = 0;
				RdrTexParams* rtex = rdrStartUpdateTextureFromSurface(
						gfx_state.currentDevice->rdr_device, texture->tex_handle, RTEX_BGRA_U8,
						scratchSurface, SBUF_0,
						MIN( texture->width, scratchSurface->width_nonthread ),
						MIN( texture->height, scratchSurface->height_nonthread ),
						&imageByteCount );
				rtex->debug_texture_backpointer = texture;
				rdrEndUpdateTexture( gfx_state.currentDevice->rdr_device );
				texRecordNewMemUsage( texture, TEX_MEM_VIDEO, getTextureMemoryUsageEx( RTEX_2D, RTEX_BGRA_U8, texture->realWidth, texture->realHeight, texGetDepth( texture ), false, false, false ));
			}
			if( info->notifyBytesF ) {
				if( isHeadshotsFinished ) {
					int width = -1;
					int height = -1;
					U8* data = rdrGetTextureData(
							gfx_state.currentDevice->rdr_device,
							texture->tex_handle,
							RTEX_2D, RTEX_BGRA_U8, &width, &height );
					if( !(width == texture->width && height == texture->height) ) {
						Errorf( "Headshot bytes not of expected size, got %dx%d, expected %dx%d",
								width, height, texture->width, texture->height );
					}
					info->notifyBytesF( info->notifyBytesData, data, width, height, (char **)&info->esExtraInfo );
					free( data );
				}
			}

			gfxEndSection();
		}

		if( isHeadshotsFinished && !gfxHeadshotAlphaDebug ) {
			for( it = eaSize( &headshotAlphaActionsThisFrame ) - 1; it >= 0; --it ) {
				HeadshotInfo* info = headshotAlphaActionsThisFrame[ it ]->info;

				if( info != NULL ) {
					if( info->notifyBytesF == NULL ) {
						eaFindAndRemove( &headshotAlphaUnprocessedList, info );
					}
					if( info->notifyF ) {
						info->notifyF( info->notifyData );
					}
					if( info->notifyBytesF != NULL ) {
						gfxHeadshotRelease( info->texture );
					}
				}
			}
		}
	}

	// HeadshotAction pass

	{
		int it;
		for( it = 0; it != eaSize( &headshotActionsThisFrame ); ++it ) {
			HeadshotAction* action = headshotActionsThisFrame[ it ];
			HeadshotInfo* info = action->info;

			if( info && info->notifyBytesF ) {
				if( isHeadshotsFinished && !gfxHeadshotInfoWantsAlpha( info )) {
					int width = -1;
					int height = -1;
					U8* data = rdrGetTextureData(
							gfx_state.currentDevice->rdr_device,
							info->texture->tex_handle,
							RTEX_2D, RTEX_BGRA_U8, &width, &height );
					
					if( !(width == info->texture->width && height == info->texture->height) ) {
						Errorf( "Headshot bytes not of expected size, got %dx%d, expected %dx%d",
								width, height, info->texture->width, info->texture->height );
					}
					info->notifyBytesF( info->notifyBytesData, data, width, height, (char **)&info->esExtraInfo );
					free( data );
				}
			}
		}

		if( isHeadshotsFinished ) {
			for( it = eaSize( &headshotActionsThisFrame ) - 1; it >= 0; --it ) {
				HeadshotInfo* info = headshotActionsThisFrame[ it ]->info;

				if( info != NULL && info->type != HETY_ANIMATED_COSTUME && info->type != HETY_ANIMATED_GROUP ) {
					assert( eaFind( &headshotActiveList, info ) != -1 );
					if( gfxHeadshotInfoWantsAlpha( info )) {
						eaFindAndRemove( &headshotUnprocessedList, info );
						eaPush( &headshotAlphaUnprocessedList, info );
					} else {
						if( info->notifyBytesF == NULL ) {
							eaFindAndRemove( &headshotUnprocessedList, info );
						}
						if( info->notifyF ) {
							info->notifyF( info->notifyData );
						}
						if( info->notifyBytesF != NULL ) {
							gfxHeadshotRelease( info->texture );
						}
					}
				}
			}
		}
	}

	if( isHeadshotsFinished ) {
		if( eaSize( &headshotUnprocessedList ) || eaSize( &headshotAlphaUnprocessedList )) {
			headshotRequestedFramesAgo = 0;
		}
	}
	
	gfxHeadshotAssertConsistantState();
}

/// This function MUST be called every frame to clean up headshot
/// state.
void gfxUFHeadshotCleanup( void )
{
	eaClearEx( &headshotActionsThisFrame, NULL );
	{
		int it;
		for( it = 0; it != eaSize( &headshotAlphaActionsThisFrame ); ++it ) {
			eaPush( &headshotTexturePool, headshotAlphaActionsThisFrame[ it ]->srcBlackTex );
			eaPush( &headshotTexturePool, headshotAlphaActionsThisFrame[ it ]->srcGrayTex );
		}
	}
	eaClearEx( &headshotAlphaActionsThisFrame, NULL );
	gfxHeadshotAssertConsistantState();
}

const char *gfxHeadshotGetCostumeName( BasicTexture* texture )
{
	int it;

	for( it = eaSize( &headshotActiveList ) - 1; it >= 0; --it ) {
		HeadshotInfo* headshot = headshotActiveList[ it ];

		if( (headshot->texture == texture) && (headshot->type == HETY_COSTUME || headshot->type == HETY_ANIMATED_COSTUME)) {
			return headshot->costume.costumeName;
		}
	}

	return NULL;
}

bool gfxHeadshotGetCostumeBounds( BasicTexture* texture, Vec3 Min, Vec3 Max )
{
	HeadshotInfo* headshotInfo = NULL;
	int it;

	for( it = eaSize( &headshotAnimatedList ) - 1; it >= 0; --it ) {
		HeadshotInfo* headshot = headshotAnimatedList[ it ];

		if( (headshot->texture == texture) && (headshot->type == HETY_COSTUME || headshot->type == HETY_ANIMATED_COSTUME)) {
			headshotInfo = headshot;
			break;
		}
	}

	if ( !headshotInfo ) {
		for( it = eaSize( &headshotActiveList ) - 1; it >= 0; --it ) {
			HeadshotInfo* headshot = headshotActiveList[ it ];

			if( (headshot->texture == texture) && (headshot->type == HETY_COSTUME || headshot->type == HETY_ANIMATED_COSTUME)) {
				headshotInfo = headshot;
				break;
			}
		}
	}

	if ( headshotInfo ) {
		if ( headshotInfo->costume.playerSkel ) {
			dynSkeletonGetVisibilityExtents( headshotInfo->costume.playerSkel, Min, Max, true );
			return true;
		}

		copyVec3(zerovec3, Min);
		copyVec3(zerovec3, Max);
		return false;
	}

	copyVec3(zerovec3, Min);
	copyVec3(zerovec3, Max);
	return false;
}

/// Checks to see if the given TEXTURE is the currently animated texture.
///
/// If the given TEXTURE is NULL then it will check there is any headshot being animated.
bool gfxHeadshotIsAnimatedCostume( BasicTexture* texture )
{
	int it;

	for( it = eaSize(&headshotAnimatedList) - 1; it >= 0; --it ) {
		HeadshotInfo* info = headshotAnimatedList[ it ];

		if ( info->type == HETY_ANIMATED_COSTUME && ( !texture || texture == info->texture ) ) {
			return true;
		}
	}

	return false;
}

/// Raise the priority of a specific headshot.  Returns if the
/// headshot has finished.
///
/// You want to call this once a frame, on all headshots currently
/// visible.  This makes sure that those headshots finish sooner than
/// any other headshot.
bool gfxHeadshotRaisePriority( BasicTexture* texture )
{
	int it;

	
	for( it = eaSize( &headshotAlphaUnprocessedList ) - 1; it >= 0; --it ) {
		HeadshotInfo* headshot = headshotAlphaUnprocessedList[ it ];

		if( headshot->texture == texture ) {
			eaMove( &headshotAlphaUnprocessedList, eaSize( &headshotAlphaUnprocessedList ) - 1, it );
			gfxHeadshotAssertConsistantState();
			return false;
		}
	}
	for( it = eaSize( &headshotUnprocessedList ) - 1; it >= 0; --it ) {
		HeadshotInfo* headshot = headshotUnprocessedList[ it ];

		if( headshot->texture == texture ) {
			eaMove( &headshotUnprocessedList, eaSize( &headshotUnprocessedList ) - 1, it );
			gfxHeadshotAssertConsistantState();
			return false;
		}
	}

	gfxHeadshotAssertConsistantState();
	return true;
}

/// Lower the priority of a specific headshot.  Returns if the
/// headshot has finished.
bool gfxHeadshotLowerPriority( BasicTexture* texture )
{
	int it;


	for( it = eaSize( &headshotAlphaUnprocessedList ) - 1; it >= 0; --it ) {
		HeadshotInfo* headshot = headshotAlphaUnprocessedList[ it ];

		if( headshot->texture == texture ) {
			eaMove( &headshotAlphaUnprocessedList, 0, it );
			gfxHeadshotAssertConsistantState();
			return false;
		}
	}
	for( it = eaSize( &headshotUnprocessedList ) - 1; it >= 0; --it ) {
		HeadshotInfo* headshot = headshotUnprocessedList[ it ];

		if( headshot->texture == texture ) {
			eaMove( &headshotUnprocessedList, 0, it );
			gfxHeadshotAssertConsistantState();
			return false;
		}
	}

	gfxHeadshotAssertConsistantState();
	return true;
}

bool gfxHeadshotFlagForUI(BasicTexture* pTexture)
{
	int i;
	for (i = eaSize(&headshotAnimatedList)-1; i >= 0; i--)
	{
		HeadshotInfo* pInfo = headshotAnimatedList[i];
		if (pInfo->texture == pTexture)
		{
			pInfo->bForUI = true;
			return true;
		}
	}
	for (i = eaSize(&headshotActiveList)-1; i >= 0; i--)
	{
		HeadshotInfo* pInfo = headshotActiveList[i];
		if (pInfo->texture == pTexture)
		{
			pInfo->bForUI = true;
			return true;
		}
	}
	return false;
}

static WorldRegion * gfxHeadshotLookupRegionForInfo( const HeadshotInfo* info, const Vec3 camPos )
{
	WorldRegion * region;
	if( info->regionId >= 0 ) {
		char buffer[1024];
		sprintf( buffer, "Headshot Region %d", info->regionId );
		region = worldGetTempWorldRegionByName(buffer);
		zmapRegionSetOverrideCubeMap( NULL, region, allocAddString( HEADSHOT_CUBEMAP_NAME ));
	} else {
		region = worldGetWorldRegionByPos( camPos );
	}
	return region;
}

static void gfxHeadshotRestoreSunlight(WorldGraphicsData *world_data)
{
	headshotSunLight = world_data->sun_light;
	headshotSunLight2 = world_data->sun_light_2;
	world_data->sun_light = realRegionSunLight;
	world_data->sun_light_2 = realRegionSunLight2;
}

/// This should be the first command in any headshot rendering
/// graphics commands.
///
/// This begins the headshot action and sets up other misc camera
/// information.
static WorldRegion *gfxHeadshotBegin( HeadshotInfo* info, const Vec3 camPos, const Vec3 camDir, const HeadshotOrthoParams *ortho )
{
	RdrSurface* scratchSurface = gfxHeadshotScratchSurface( info );

	BasicTexture* background = info->background;
	float* bgTexCoords = info->bgTexCoords;
	
	Mat4 cameraMatrix;
	WorldRegion *region;
	int headshotIt = eaSize( &headshotAlphaActionsThisFrame ) * 2 + eaSize( &headshotActionsThisFrame );
	bool isScene = (info->type == HETY_SCENE || info->type == HETY_CONSTRUCTED_SCENE);
	bool isConstructedScene = (info->type == HETY_CONSTRUCTED_SCENE);
	bool isGroup = (info->type == HETY_GROUP);
	const float* actualCamPos;

	WorldGraphicsData *world_data = worldGetWorldGraphicsData();

	float oldNearPlaneDist = gfx_state.near_plane_dist;
	float oldFarPlaneDist = gfx_state.far_plane_dist;

	BasicTexture* texture;

	realRegionSunLight = world_data->sun_light;
	realRegionSunLight2 = world_data->sun_light_2;
	world_data->sun_light = headshotSunLight;
	world_data->sun_light_2 = headshotSunLight2;

	if( headshotOverrideDestTexture ) {
		texture = headshotOverrideDestTexture;
	} else {
		// Pull out from the texture pool, if we can
		if( !SAFE_MEMBER( info->texture->loaded_data, tex_memory_use[ TEX_MEM_VIDEO ])) {
			gfxHeadshotSwapWithCompatibleTexture( info->texture );
		}
		texture = info->texture;
	}
	
	devassert(texture->flags & TEX_SCRATCH);
	if( !(texture->flags & TEX_SCRATCH) ) {
		// Something very very wrong has happened with my texture
		// without ever notifying me.  Bail out early here, the
		// alternative is to crash.
		gfxHeadshotRestoreSunlight(world_data);
		return NULL;
	}

	// prevent wasted effort setting up an action that we cannot start, and modifications to 
	// GraphicsLib state, such as the active ambient render light.
	if( !gfxStartActionQuery( GfxAction_Headshot, 1 )) {
		gfxHeadshotRestoreSunlight(world_data);
		return NULL;
	}


	gfxHeadshotInit();

	assert(headshotIt < eaSize( &headshotCameraViewList ));
	assert(headshotIt < eaSize( &headshotCameraControllerList ));
	assert(headshotIt < eaSize( &headshotAmbientLightList ));

	if( isConstructedScene && camPos == NULL ) {
		actualCamPos = info->constructedScene.camController->camcenter;
	} else {
		actualCamPos = camPos;
		// MJF (July/24/2012) - This is certainly a kludge fix.  We
		// don't want to put the far plane too close if the campos is
		// way out there
		if( lengthVec3( camPos ) < 500 ) {
			gfx_state.near_plane_dist = 0.1;
			gfx_state.far_plane_dist = 1000;
		} else {
			gfx_state.near_plane_dist = 100;
			gfx_state.far_plane_dist = 5000;
		}
	}
	
	gfxAuxDeviceSaveParams( true );
	gfxSetActiveCameraView( headshotCameraViewList[ headshotIt ], false );
	if( isConstructedScene ) {
		gfxSetActiveCameraController( info->constructedScene.camController, false );
	} else {
		gfxSetActiveCameraController( headshotCameraControllerList[ headshotIt ], false );

		headshotCameraControllerList[ headshotIt ]->projection_fov = (info->fovY > 0 ? info->fovY : DEFAULT_FOV);
	}
	if( !isScene || isConstructedScene ) {
		gfxSetAmbientLight( headshotAmbientLightList[ headshotIt ]);
	}

	if( !isConstructedScene ) {
		if (ortho)
		{
			headshotCameraControllerList[ headshotIt ]->ortho_mode_ex = true;
			copyVec3(ortho->pyr, headshotCameraControllerList[ headshotIt ]->campyr);
			headshotCameraControllerList[ headshotIt ]->camdist = 0;
			headshotCameraControllerList[ headshotIt ]->inited = true;

			headshotCameraControllerList[ headshotIt ]->ortho_aspect = 1;
			headshotCameraControllerList[ headshotIt ]->ortho_width = ortho->world_w;
			headshotCameraControllerList[ headshotIt ]->ortho_height = ortho->world_h;
			headshotCameraControllerList[ headshotIt ]->ortho_cull_width = 0;
			headshotCameraControllerList[ headshotIt ]->ortho_cull_height = 0;
			copyVec3(ortho->center, headshotCameraControllerList[ headshotIt ]->camcenter);
			headshotCameraControllerList[ headshotIt ]->ortho_far = ortho->far_plane;
			headshotCameraControllerList[ headshotIt ]->ortho_near = ortho->near_plane;
		}
		else
		{
			Vec3 camLookAtVec;
			scaleVec3( camDir, -1, camLookAtVec );
			camLookAt( camLookAtVec, cameraMatrix );
			copyVec3( camPos, cameraMatrix[3] );
			headshotCameraControllerList[ headshotIt ]->ortho_mode_ex = false;
			frustumSetCameraMatrix( &headshotCameraViewList[ headshotIt ]->new_frustum, cameraMatrix );
		}
	}

	region = gfxHeadshotLookupRegionForInfo( info, actualCamPos );

	if( isScene ) {
		gfxCameraControllerSetSkyOverride( headshotCameraControllerList[ headshotIt ], NULL, NULL );
	} else if( info->sky ) {
		gfxCameraControllerSetSkyOverride( headshotCameraControllerList[ headshotIt ], info->sky, NULL );
	} else {
		gfxCameraControllerSetSkyOverride( headshotCameraControllerList[ headshotIt ], HEADSHOT_SKY_NAME, NULL );
	}

	if( !isConstructedScene && !ortho ) {
		gfxSetActiveProjection( headshotCameraControllerList[ headshotIt ]->projection_fov,
								(float)texture->width / texture->height );
	}
	if( !isScene ) {
		SkyInfo* headshotSky = gfxSkyFindSky( info->sky ? info->sky : HEADSHOT_SKY_NAME );
		Vec4 clearColor;

		if( headshotOverrideBGColor ) {
			colorToVec3( clearColor, *headshotOverrideBGColor );
		} else {
			colorToVec3( clearColor, info->bgColor );
		}

		if( headshotSky && eaSize( &headshotSky->sunValues ) > 0 ) {
			if (headshotSky->lightBehaviorValues) {
				scaleVec3( clearColor, headshotSky->lightBehaviorValues[ 0 ]->lightRange, clearColor );
			} else {
				scaleVec3( clearColor, 2.0f, clearColor );
			}
		}
		clearColor[3] = 1;
		
		gfxActiveCameraControllerOverrideClearColor( clearColor );
	} else {
		gfxActiveCameraControllerOverrideClearColor( NULL );
	}

	gfxFlipCameraFrustum( headshotCameraViewList[ headshotIt ] );

	headshotCameraViewList[ headshotIt ]->adapted_light_range_inited = 0;
	gfxRunActiveCameraController((float)texture->width / texture->height, region);

	if( !isScene ) {
		// Force the sky to snap to its data
		gfxSkyClearVisibleSky( headshotCameraViewList[ headshotIt ]->sky_data );
		headshotCameraViewList[ headshotIt ]->adapted_light_range = 2;
	}

	gfxEnableCameraLight(false);

	// To keep this from ever loading a shader, (MU-14659), make sure
	// the parameters here are in sync with gfxStartMainFrameAction.
	if( !gfxStartAction( scratchSurface, 0, region,
						 headshotCameraViewList[ headshotIt ],
						 gfx_state.currentCameraController,
						 false, false, !isScene, false, isScene, 
						 isGroup ? info->group.enableShadows : false, true,
						 isConstructedScene ? info->constructedScene.allowOutlines : true, false,
						 GfxAction_Headshot, gfxHeadshotUseSunIndoors, HEADSHOT_VISSCALE ))
	{
		gfxAuxDeviceSaveParams( false );
		gfxEndSection();

		gfx_state.near_plane_dist = oldNearPlaneDist;
		gfx_state.far_plane_dist = oldFarPlaneDist;
		
		gfxHeadshotRestoreSunlight(world_data);
		return NULL;
	}

	// Turn off indoor ambient light for map snaps.
	gfx_state.currentAction->no_indoor_ambient = gfxHeadshotUseSunIndoors;

	gfx_state.currentAction->override_usage_flags = WL_FOR_PREVIEW_INTERNAL;
	gfx_state.currentAction->override_time = ( info->type == HETY_ANIMATED_COSTUME || info->type == HETY_ANIMATED_GROUP );

	// Viewporting
	gfx_state.currentAction->postRenderBlitOutputTexture = texture;
	gfxHeadshotInfoViewport( gfx_state.currentAction->renderViewport, info );
	copyVec4( gfx_state.currentAction->renderViewport, gfx_state.currentAction->postRenderBlitViewport );
	gfx_state.currentAction->postRenderBlitDebugThumbnail = gfxHeadshotDebugThumbnail;

	// background texture
	if( background ) {
		RdrDrawList* draw_list = gfx_state.currentAction->gdraw.draw_list;
		RdrDrawablePrimitive* quad = rdrDrawListAllocPrimitive( draw_list, RTYPE_PRIMITIVE, false );

		if (quad)
		{
			Frustum* frustum = &headshotCameraViewList[ headshotIt ]->frustum;
			Vec3 deltaX;
			Vec3 deltaY;
			Vec3 deltaZ;
			F32 u_mult, v_mult;
		
			if( !gfx_state.currentCameraController->ortho_mode_ex ) {
				// [STO-6881] Some slight floating point error
				float fudgeFactor = 1.05;
				float headshotZ = -950;
				Vec3 tempDeltaX = { headshotZ * frustum->htan * fudgeFactor, 0, 0 };
				Vec3 tempDeltaY = { 0, headshotZ * frustum->vtan * fudgeFactor, 0 };
				Vec3 tempDeltaZ = { 0, 0, headshotZ };
				assert(!isConstructedScene);
				mulVecMat3( tempDeltaX, frustum->cammat, deltaX );
				mulVecMat3( tempDeltaY, frustum->cammat, deltaY );
				mulVecMat3( tempDeltaZ, frustum->cammat, deltaZ );
			} else {
				float epsilon = 0.005 * (gfx_state.currentCameraController->ortho_far
										 - gfx_state.currentCameraController->ortho_near);
				float fudgeFactor = 1.01;
				Vec3 tempDeltaX = { fudgeFactor * -gfx_state.currentCameraController->ortho_width / 2, 0, 0 };
				Vec3 tempDeltaY = { 0, fudgeFactor * gfx_state.currentCameraController->ortho_height / 2, 0 };
				// our camera looks down -Z, so "far from the camera in Z" is a large negative number, so we need to negate the positive ortho far
				Vec3 tempDeltaZ = { 0, 0, -gfx_state.currentCameraController->ortho_far + epsilon };
				mulVecMat3( tempDeltaX, frustum->cammat, deltaX );
				mulVecMat3( tempDeltaY, frustum->cammat, deltaY );
				mulVecMat3( tempDeltaZ, frustum->cammat, deltaZ );
			}

			quad->in_3d = true;
			quad->type = RP_QUAD;
			quad->filled = true;
			quad->tonemapped = false;
			quad->tex_handle = texDemandLoad( background, 0.f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY, white_tex);

			if (background->actualTexture->width > background->actualTexture->realWidth)
			{
				// Must be a MIP'd texture running at a lower MIP
				// MIP'd textures must be power of 2
				u_mult = ((float)background->actualTexture->width) / ((float)pow2(background->actualTexture->width));
				v_mult = ((float)background->actualTexture->height) / ((float)pow2(background->actualTexture->height));
			} else {
				u_mult = ((float)background->actualTexture->width) / ((float)background->actualTexture->realWidth);
				v_mult = ((float)background->actualTexture->height) / ((float)background->actualTexture->realHeight);
			}

			{
				Vec3 temp;
				copyVec3( actualCamPos, temp );
				subVec3( temp, deltaX, temp );
				subVec3( temp, deltaY, temp );
				addVec3( temp, deltaZ, temp );
				copyVec3( temp, quad->vertices[ 0 ].pos );
				setVec2( quad->vertices[ 0 ].texcoord, bgTexCoords[0] * u_mult, bgTexCoords[1] * v_mult );
			}
			{
				Vec3 temp;
				copyVec3( actualCamPos, temp );
				addVec3( temp, deltaX, temp );
				subVec3( temp, deltaY, temp );
				addVec3( temp, deltaZ, temp );
				copyVec3( temp, quad->vertices[ 1 ].pos );
				setVec2( quad->vertices[ 1 ].texcoord, bgTexCoords[2] * u_mult, bgTexCoords[1] * v_mult );
			}
			{
				Vec3 temp;
				copyVec3( actualCamPos, temp );
				addVec3( temp, deltaX, temp );
				addVec3( temp, deltaY, temp );
				addVec3( temp, deltaZ, temp );
				copyVec3( temp, quad->vertices[ 2 ].pos );
				setVec2( quad->vertices[ 2 ].texcoord, bgTexCoords[2] * u_mult, bgTexCoords[3] * v_mult );
			}
			{
				Vec3 temp;
				copyVec3( actualCamPos, temp );
				subVec3( temp, deltaX, temp );
				addVec3( temp, deltaY, temp );
				addVec3( temp, deltaZ, temp );
				copyVec3( temp, quad->vertices[ 3 ].pos );
				setVec2( quad->vertices[ 3 ].texcoord, bgTexCoords[0] * u_mult, bgTexCoords[3] * v_mult );
			}

			colorToVec4( quad->vertices[0].color, ColorWhite );
			colorToVec4( quad->vertices[1].color, ColorWhite );
			colorToVec4( quad->vertices[2].color, ColorWhite );
			colorToVec4( quad->vertices[3].color, ColorWhite );

			rdrDrawListAddPrimitive( draw_list, quad, RST_AUTO, ROC_PRIMITIVE );
		}
	}

	{
		HeadshotAction* accum = calloc( 1, sizeof( *accum ));
		accum->info = info;

		eaPush( &headshotActionsThisFrame, accum );
	}

	gfx_state.near_plane_dist = oldNearPlaneDist;
	gfx_state.far_plane_dist = oldFarPlaneDist;
	return region;
}

/// This should be the last command in any headshot rendering
/// graphics commands.
///
/// This cleans up all the setup done by gfxHeadshotBegin().
static void gfxHeadshotEnd(void)
{
	WorldGraphicsData *world_data = worldGetWorldGraphicsData();

	gfxFinishAction();
	gfxSetAmbientLight( NULL );
	gfxEnableCameraLight(true);

	gfxHeadshotRestoreSunlight(world_data);

	gfxAuxDeviceSaveParams( false );
}

/// Capture a headshot of COSTUME and save that into TEXTURE.  If BFG
/// is specified, use those bitfields.
///
/// This headshot will put COSTUME into a headshot pose before
/// capturing.
BasicTexture* gfxHeadshotCaptureCostume_dbg( const char* headshotName, int width, int height, WLCostume* costume, BasicTexture* background, const char* framename, Color bgColor, bool forceBodyshot, DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance, float fovY, const char* sky, PCFXTemp ***peaExtraFX, HeadshotNotifyBytesF notifyBytesF, UserData notifyBytesData MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );

	if( !costume || gfxHeadshotDisable ) {
		gfxHeadshotAssertConsistantState();
		texGenFreeNextFrame( texture );
		return NULL;
	}

	{
		HeadshotInfo* info = gfxHeadshotInfoCostumeCreate( texture, costume, background, framename, bgColor, forceBodyshot, bfg, pcAnimKeyword, pchStance, sky, NULL, NULL, 0.1f, NULL, fovY, NULL, NULL, peaExtraFX MEM_DBG_PARMS_CALL );
		if( info == NULL ) {
			gfxHeadshotAssertConsistantState();
			texGenFreeNextFrame( texture );
			return NULL;
		}
		info->notifyBytesF = notifyBytesF;
		info->notifyBytesData = notifyBytesData;

		eaPush( &headshotUnprocessedList, info );
		eaPush( &headshotActiveList, info );
		headshotRequestedFramesAgo = 0;
	}
	
	gfxHeadshotAssertConsistantState();

	return texture;
}

/// Generic dispatch to recreate a HEADSHOT-INFO, updating any cached
/// info.
static void gfxHeadshotRecreate( HeadshotInfo** info )
{
	// right now, only costumes need any special processing
	if( (*info)->type == HETY_COSTUME || (*info)->type == HETY_ANIMATED_COSTUME ) {
		HeadshotType type = (*info)->type;
		BasicTexture* texture = (*info)->texture;
		BasicTexture* background = (*info)->background;
		Color bgColor = (*info)->bgColor;
		char* sky;
		float fovY = (*info)->fovY;
		
		WLCostume* costume = RefSystem_ReferentFromString( "Costume", (*info)->costume.costumeName );
		const char* framename = (*info)->costume.framename;
		bool forceBodyshot = (*info)->costume.forceBodyshot;
		DynBitFieldGroup bfg = (*info)->costume.bfg;
		const char* pcAnimKeyword = (*info)->costume.pcAnimKeyword;
		const char* pchStance = (*info)->costume.pchStance;
		float animDelta = (*info)->costume.animDelta;
		bool useOverrideCamera = (*info)->costume.useOverrideCamera;
		char* ppExtraInfo = NULL;
		Vec3 camPos;
		Vec3 camDir;

		if( (*info)->sky ) {
			strdup_alloca( sky, (*info)->sky );
		} else {
			sky = NULL;
		}
		estrCopy(&ppExtraInfo, &(*info)->esExtraInfo);

		if( useOverrideCamera ) {
			copyVec3( (*info)->costume.overrideCamPos, camPos );
			copyVec3( (*info)->costume.overrideCamDir, camDir );
		}

		
		// It's possible the costume has been removed from the
		// dictionary already, in which case we can just not update
		// the headshot
		if( !costume ) {
			(*info)->needsRecreate = false;
			return;
		}

		eaFindAndRemove( &headshotUnprocessedList, *info );
		eaFindAndRemove( &headshotAlphaUnprocessedList, *info );
		eaFindAndRemove( &headshotAnimatedList, *info );

		{
			HeadshotInfo* newInfo = gfxHeadshotInfoCostumeCreate( texture, costume, background, framename, bgColor, forceBodyshot, &bfg, pcAnimKeyword, pchStance, sky,
																  (useOverrideCamera ? camPos : NULL), (useOverrideCamera ? camDir : NULL),
																  animDelta, &ppExtraInfo, fovY, (*info)->updateCameraF, (*info)->updateCameraData, &(*info)->costume.eaFX,
																  (*info)->fname, (*info)->line );
			if( !newInfo ) {
				Errorf( "Unable to recreate headshot" );
				(*info)->needsRecreate = false;
				return;
			}

			if (newInfo->type == HETY_ANIMATED_COSTUME)
			{
				wlPerfStartSkelBudget( world_perf_info.pCounts );
				dynSkeletonUpdate( newInfo->costume.playerSkel, 1.0, world_perf_info.pCounts );
				wlPerfEndSkelBudget( world_perf_info.pCounts );
				dynFxManagerUpdate( newInfo->costume.fxMan, DYNFXTIME( 1.0 ));
				gfxHeadshotSkeletonCalcCamPosAndDir( headshotAnimatedCamPos, headshotAnimatedCamDir, newInfo );
			}

			gfxHeadshotInfoDestroyEx( *info, false, false );
			*info = newInfo;
			(*info)->type = type;
		}
	} else {
		eaFindAndRemove( &headshotUnprocessedList, *info );
		eaFindAndRemove( &headshotAlphaUnprocessedList, *info );
		eaFindAndRemove( &headshotAnimatedList, *info );
	}
	
	if( (*info)->type != HETY_ANIMATED_COSTUME && (*info)->type != HETY_ANIMATED_GROUP ) {
		eaPush( &headshotUnprocessedList, *info );
	} else {
		eaPush( &headshotAnimatedList, *info );
	}
}

/// Generic dispatch on capturing a HEADSHOT-INFO.
static void gfxHeadshotCapture1( HeadshotInfo* info, bool reopenTempEntry )
{
	switch( info->type ) {
		case HETY_COSTUME: case HETY_ANIMATED_COSTUME:
			gfxHeadshotCaptureCostume1( info );

		xcase HETY_GROUP: case HETY_ANIMATED_GROUP:
			gfxHeadshotCaptureGroup1( info, reopenTempEntry );

		xcase HETY_SCENE:
			gfxHeadshotCaptureScene1( info );

		xcase HETY_CONSTRUCTED_SCENE:
			gfxHeadshotCaptureConstructedScene1( info );

		xcase HETY_MODEL:
		case HETY_MATERIAL:
			gfxHeadshotCaptureModel1( info );

		xdefault:
			FatalErrorf( "You must direct the code to an update function, HeadshotType=%d", info->type );
	}
}

/// See GFX-HEADSHOT-CAPTURE-COSTUME.
///
/// This does the same thing, without registering a HEADSHOT-INFO.
void gfxHeadshotCaptureCostume1( HeadshotInfo* info )
{
	Vec3 camPos;
	Vec3 camDir;

	assert( info->type == HETY_COSTUME || info->type == HETY_ANIMATED_COSTUME );

	wlPerfStartSkelBudget( world_perf_info.pCounts );
	if( info->type == HETY_ANIMATED_COSTUME ) {
		float dt = gfx_state.frame_time;
		if( gfxHeadshotAnimationIsPaused ) {
			dt = 0;
		}
		dynSkeletonUpdate( info->costume.playerSkel, dt, world_perf_info.pCounts );
		dynFxManagerUpdate( info->costume.fxMan, DYNFXTIME( dt ));
	} else {
		dynSkeletonUpdate( info->costume.playerSkel, 0.0, world_perf_info.pCounts );
	}
	wlPerfEndSkelBudget( world_perf_info.pCounts );

	gfxHeadshotSkeletonCalcCamPosAndDir( camPos, camDir, info );
	headshotCaptureSkeleton( camPos, camDir, info );
}

static bool gfxHeadshotBFGEqual( DynBitFieldGroup* bfg1, DynBitFieldGroup* bfg2 )
{
	DynBitFieldGroup* effectiveBFG1;
	DynBitFieldGroup* effectiveBFG2;

	gfxHeadshotInit();
	
	if( !bfg1 ) {
		effectiveBFG1 = &headshotDefaultBFG;
	} else {
		effectiveBFG1 = bfg1;
	}
	if( !bfg2 ) {
		effectiveBFG2 = &headshotDefaultBFG;
	} else {
		effectiveBFG2 = bfg2;
	}

	return (dynBitFieldsAreEqual( &effectiveBFG1->flashBits, &effectiveBFG2->flashBits )
			&& dynBitFieldsAreEqual( &effectiveBFG1->toggleBits, &effectiveBFG2->toggleBits ));
}

/// Capture a headshot of COSTUME and save that into TEXTURE.  Will
/// animate the costume.  Only one costume can be animated at a time,
/// for memory reasons.
///
/// If you pass in a costume OTHER than the one you wanted to animate,
/// this will automatically restart the animation.
///
/// This headshot will put COSTUME into a headshot pose before capturing.
BasicTexture* gfxHeadshotCaptureAnimatedCostume_dbg(
		const char* headshotName, int width, int height, WLCostume* costume, BasicTexture* background, const char* framename,
		Color bgColor, bool forceBodyshot, DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance, float fovY, const char* sky
		MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );
	
	if( !costume || gfxHeadshotDisable ) {
		gfxHeadshotAssertConsistantState();
		return texture;
	}

	{
		float animDelta;
		HeadshotInfo* info;

		if( gfxHeadshotAnimationIsPaused ) {
			animDelta = 0;
		} else {
			animDelta = 0.1;
		}			
		info = gfxHeadshotInfoCostumeCreate( texture, costume, background, framename, bgColor, forceBodyshot, bfg, pcAnimKeyword, pchStance, sky, NULL, NULL, animDelta, NULL, fovY, NULL, NULL, NULL MEM_DBG_PARMS_CALL );
		if( info == NULL ) {
			gfxHeadshotAssertConsistantState();
			return NULL;
		}
		info->type = HETY_ANIMATED_COSTUME;

		devassertmsg(eaSize(&headshotAnimatedList) < HEADSHOT_MAX_ANIMATED, "Too many animated headshots");
		eaPush( &headshotAnimatedList, info );

		if( !gfxHeadshotAnimationIsPaused ) {
			wlPerfStartSkelBudget( world_perf_info.pCounts );
			dynSkeletonUpdate( info->costume.playerSkel, 1.0, world_perf_info.pCounts );
			wlPerfEndSkelBudget( world_perf_info.pCounts );
			dynFxManagerUpdate( info->costume.fxMan, DYNFXTIME( 1.0 ));
		}
		gfxHeadshotSkeletonCalcCamPosAndDir( headshotAnimatedCamPos, headshotAnimatedCamDir, info );
	}
	
	gfxHeadshotAssertConsistantState();

	return texture;
}

/// Capture a headshot of COSTUME and save that into TEXTURE.  However,
/// the camera position may be controlled by the optional callback function.
/// Will animate the costume.  Only one costume can be animated at a time,
/// for memory reasons.
///
/// If you pass in a costume OTHER than the one you wanted to animate,
/// this will automatically restart the animation.
///
/// This headshot will put COSTUME into a headshot pose before capturing.
///
/// For the same parameters, this should create an animated headshot that
/// is equivalent to gfxHeadshotCaptureCostumeScene.
BasicTexture* gfxHeadshotCaptureAnimatedCostumeScene_dbg(
	const char* headshotName, int width, int height, WLCostume* costume, BasicTexture* background, const char* framename,
	Color bgColor, bool forceBodyshot, DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance, HeadshotUpdateCameraF updateCameraF, UserData updateCameraData, float fovY, const char* sky, PCFXTemp ***peaExtraFX MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );

	if( !costume || gfxHeadshotDisable ) {
		gfxHeadshotAssertConsistantState();
		texGenFreeNextFrame( texture );
		return NULL;
	}

	{
		float animDelta;
		HeadshotInfo* info;

		if( gfxHeadshotAnimationIsPaused ) {
			animDelta = 0;
		} else {
			animDelta = 0.1;
		}			
		info = gfxHeadshotInfoCostumeCreate( texture, costume, background, framename, bgColor, forceBodyshot, bfg, pcAnimKeyword, pchStance, sky, NULL, NULL, animDelta, NULL, fovY, updateCameraF, updateCameraData, peaExtraFX MEM_DBG_PARMS_CALL );
		
		if( info == NULL ) {
			gfxHeadshotAssertConsistantState();
			texGenFreeNextFrame( texture );
			return NULL;
		}
		info->type = HETY_ANIMATED_COSTUME;

		devassertmsg(eaSize(&headshotAnimatedList) < HEADSHOT_MAX_ANIMATED, "Too many animated headshots");
		eaPush( &headshotAnimatedList, info );

		if( !gfxHeadshotAnimationIsPaused ) {
			wlPerfStartSkelBudget( world_perf_info.pCounts );
			dynSkeletonUpdate( info->costume.playerSkel, 1.0, world_perf_info.pCounts );
			wlPerfEndSkelBudget( world_perf_info.pCounts );
			dynFxManagerUpdate( info->costume.fxMan, DYNFXTIME( 1.0 ));
		}
		gfxHeadshotSkeletonCalcCamPosAndDir( headshotAnimatedCamPos, headshotAnimatedCamDir, info );
	}

	gfxHeadshotAssertConsistantState();

	return texture;
}

/// Capture a headshot of COSTUME and save that into TEXTURE.  However,
/// the camera position may be controlled by the optional callback function.
///
/// This headshot will put COSTUME into a headshot pose before capturing.
///
/// For the same parameters, this should create a static headshot that
/// is equivalent to gfxHeadshotCaptureAnimatedCostumeScene. However,
/// the camera position is specified with CAM-POS, CAM-DIR.
BasicTexture* gfxHeadshotCaptureCostumeScene_dbg(
		const char* headshotName, int width, int height, WLCostume* costume, BasicTexture* background, const char* framename,
		Color bgColor, bool forceBodyshot, DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance, Vec3 camPos, Vec3 camDir, F32 fAnimationFrame,
		float fovY, const char* sky, PCFXTemp ***peaExtraFX MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );
	
	if( !costume || gfxHeadshotDisable  ) {
		gfxHeadshotAssertConsistantState();
		texGenFreeNextFrame( texture );
		return NULL;
	}

	{
		HeadshotInfo* info = gfxHeadshotInfoCostumeCreate( texture, costume, background, framename, bgColor, forceBodyshot, bfg, pcAnimKeyword, pchStance, sky, camPos, camDir, fAnimationFrame, NULL, fovY, NULL, NULL, peaExtraFX MEM_DBG_PARMS_CALL );
		if( info == NULL ) {
			gfxHeadshotAssertConsistantState();
			texGenFreeNextFrame( texture );
			return NULL;
		}

		eaPush( &headshotUnprocessedList, info );
		eaPush( &headshotActiveList, info );
		headshotRequestedFramesAgo = 0;
	}
	
	gfxHeadshotAssertConsistantState();

	return texture;
}

void gfxHeadshotUpdateAnimBits(BasicTexture* texture, DynBitFieldGroup* bfg)
{
	if (!gConf.bNewAnimationSystem)
	{
		S32 i, s = eaSize(&headshotAnimatedList);
		for(i = 0; i < s; ++i)
		{
			HeadshotInfo* info = headshotAnimatedList[i];
			if(info->texture == texture)
			{
				dynSeqRemoveBitFieldFeed(info->costume.playerSkel, &info->costume.bfg);
				if(bfg) {
					info->costume.bfg = *bfg;
					dynSeqPushBitFieldFeed(info->costume.playerSkel, &info->costume.bfg);
					return;
				}
			}
		}
	}
}

static void gfxHeadshotUpdateExisting(	HeadshotInfo* info, BasicTexture* texture, BasicTexture* background, 
										const char* framename, Color bgColor, bool forceBodyshot, 
										DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* sky, 
										Vec3 camPos, Vec3 camDir, float animDelta, char **ppExtraInfo)
{
	gfxHeadshotInit();
	
	info->texture = texture;
	info->background = background;
	setVec4( info->bgTexCoords, 0, 0, 1, 1 );
	info->bgColor = bgColor;
	if ( info->sky )
		free( info->sky );
	info->sky = strdup(sky);

	if( eaiSize( &headshotFreeRegionIds )) {
		info->regionId = eaiPop( &headshotFreeRegionIds );
	} else {
		info->regionId = headshotRegionCounter++;
	}

	info->costume.framename = allocAddString(framename);

	info->costume.forceBodyshot = forceBodyshot;

	if (!gConf.bNewAnimationSystem)
	{
		dynSeqRemoveBitFieldFeed( info->costume.playerSkel, &info->costume.bfg );

		if( bfg ) {
			info->costume.bfg = *bfg;
			dynSeqPushBitFieldFeed( info->costume.playerSkel, &info->costume.bfg );
		} else {
			info->costume.bfg = headshotDefaultBFG;
			dynSeqPushBitFieldFeed( info->costume.playerSkel, &info->costume.bfg );
		}
	}
	else
	{
		//dynSkeletonSetCostumeStanceWord(info->costume.playerSkel, "Headshot");

		if(pcAnimKeyword)
			dynSkeletonStartGraph(info->costume.playerSkel, allocAddString(pcAnimKeyword), 0);
		//else
		//dynSkeletonStartGraph(accum->costume.playerSkel, allocAddString())
	}

	if( camPos && camDir ) {
		copyVec3( camPos, info->costume.overrideCamPos );
		copyVec3( camDir, info->costume.overrideCamDir );
		info->costume.useOverrideCamera = true;
	}
	else
	{
		info->costume.useOverrideCamera = false;
	}

	wlPerfStartSkelBudget( world_perf_info.pCounts );
	dynSkeletonUpdate( info->costume.playerSkel, animDelta, world_perf_info.pCounts );
	wlPerfEndSkelBudget( world_perf_info.pCounts );

	if(ppExtraInfo)
	{
		estrCopy(&info->esExtraInfo, ppExtraInfo);
	}
}

/// Create a new HeadshotInfo structure to save a headshot of COSTUME
/// into TEXTURE.
static HeadshotInfo* gfxHeadshotInfoCostumeCreate(
		BasicTexture* texture, WLCostume* costume, BasicTexture* background, const char* framename, Color bgColor,
		bool forceBodyshot, DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance, const char* sky, Vec3 camPos, Vec3 camDir, float animDelta, char **ppExtraInfo,
		float fovY, HeadshotUpdateCameraF updateCameraF, UserData updateCameraData, PCFXTemp ***peaExtraFX MEM_DBG_PARMS )
{
	HeadshotInfo* accum;

	WLCostume *pUseCostume = costume;
	if (costume->bMount) {
		FOR_EACH_IN_EARRAY(costume->eaSubCostumes, WLSubCostume, pSubCostume) {
			WLCostume *pChkCostume = GET_REF(pSubCostume->hSubCostume);
			if (SAFE_MEMBER(pChkCostume,bRider)) {
				pUseCostume = pChkCostume;
				break;
			}
		} FOR_EACH_END;
	}
	
	gfxHeadshotInit();

	accum = calloc( 1, sizeof( *accum ));
	accum->fname = caller_fname;
	accum->line = line;

	accum->texture = texture;
	accum->type = HETY_COSTUME;
	accum->background = background;
	setVec4( accum->bgTexCoords, 0, 0, 1, 1 );
	accum->bgColor = bgColor;
	accum->sky = strdup( sky );
	accum->fovY = fovY;
	
	if( eaiSize( &headshotFreeRegionIds )) {
		accum->regionId = eaiPop( &headshotFreeRegionIds );
	} else {
		accum->regionId = headshotRegionCounter++;
	}

	accum->costume.costumeName = allocAddString( pUseCostume->pcName );
	accum->costume.framename = allocAddString(framename);
	accum->costume.animDelta = animDelta;

	accum->costume.root = dynNodeAlloc();
	accum->costume.fxMan = dynFxManCreate( accum->costume.root, NULL,
										   NULL, eFxManagerType_Headshot, 0, PARTITION_CLIENT, false, true);
	if( !accum->costume.fxMan ) {
		Errorf( "Unable to create an FX Manager for headshot." );
		gfxHeadshotInfoDestroy( accum );
		return NULL;
	}
	
	accum->costume.playerSkel = dynSkeletonCreate( pUseCostume, true, true, false, false, true, NULL);
	if( !accum->costume.playerSkel ) {
		Errorf( "Unable to create a skeleton for headshot." );
		gfxHeadshotInfoDestroy( accum );
		return NULL;
	}
	
	accum->costume.playerDrawSkel = dynDrawSkeletonCreate(
			accum->costume.playerSkel, pUseCostume, accum->costume.fxMan, false, false, true);
	if( !accum->costume.playerDrawSkel ) {
		Errorf( "Unable to create a Draw Skeleton for headshot." );
		gfxHeadshotInfoDestroy( accum );
		return NULL;
	}
	
	accum->costume.forceBodyshot = forceBodyshot;
	dynNodeParent( accum->costume.playerSkel->pRoot, accum->costume.root );
	if (!gConf.bNewAnimationSystem)
	{
		if( bfg ) {
			accum->costume.bfg = *bfg;
			dynSeqPushBitFieldFeed( accum->costume.playerSkel, &accum->costume.bfg );
		} else {
			accum->costume.bfg = headshotDefaultBFG;
			dynSeqPushBitFieldFeed( accum->costume.playerSkel, &accum->costume.bfg );
		}
	}
	else
	{
		if (pchStance && pchStance[0])
		{
			char** tokens = NULL;
			int i;
			estrTokenize(&tokens, " ", pchStance);
			for (i = 0; i < eaSize(&tokens); i++)
			{
				dynSkeletonSetCostumeStanceWord(accum->costume.playerSkel, allocAddString(tokens[i]));
			}
			eaDestroyEString(&tokens);
		}
		dynSkeletonSetCostumeStanceWord(accum->costume.playerSkel, "Headshot");

		//restart the animation sequencers with the added stance
		dynSkeletonResetSequencers(accum->costume.playerSkel);

		if(pcAnimKeyword)
			dynSkeletonStartGraph(accum->costume.playerSkel, allocAddString(pcAnimKeyword), 0);
		//else
			//dynSkeletonStartGraph(accum->costume.playerSkel, allocAddString())
	}

	if( camPos && camDir ) {
		copyVec3( camPos, accum->costume.overrideCamPos );
		copyVec3( camDir, accum->costume.overrideCamDir );
		accum->costume.useOverrideCamera = true;
	}

	if ( updateCameraF ) {
		accum->updateCameraF = updateCameraF;
		accum->updateCameraData = updateCameraData;
	}
	
	// initial updates to get into a desired animation frame
	{
		int it;
		for( it = 0; it != 50; ++it ) {
			dynFxManagerUpdate( accum->costume.fxMan, DYNFXTIME( 0.1 ));
		}
	}
	wlPerfStartSkelBudget( world_perf_info.pCounts );
	dynSkeletonUpdate( accum->costume.playerSkel, animDelta, world_perf_info.pCounts );
	wlPerfEndSkelBudget( world_perf_info.pCounts );

	if(ppExtraInfo)
	{
		estrCopy(&accum->esExtraInfo, ppExtraInfo);
	}


	if(peaExtraFX){
		PCFXTemp *pCurrentFX;
		S32 i;
		DynAddFxParams params;
		params.bOverridePriority = false;
		params.cbJitterListSelectFunc = NULL;
		params.ePriorityOverride = edpNotSet;
		params.eSource = eDynFxSource_Costume;
		params.fHue = 0;
		params.fSaturation = 0;
		params.fValue = 0;
		params.pJitterListSelectData = NULL;
		params.pParent = NULL;
		params.pSibling = NULL;
		params.pSortBucket = NULL;
		params.pTargetRoot = NULL;
		params.pSourceRoot = NULL;
		params.uiHitReactID = 0;
		for(i = 0; i < eaSize(peaExtraFX); ++i) {
			PCFXTemp *pNewFX = StructAlloc(parse_PCFXTemp);
			pCurrentFX = (*peaExtraFX)[i];
			params.pParamBlock = StructClone(parse_DynParamBlock, pCurrentFX->pParams);
			dynAddFx(accum->costume.fxMan, pCurrentFX->pcName, &params);
			StructCopy(parse_PCFXTemp, pCurrentFX, pNewFX, 0, 0, 0);
			eaPush(&accum->costume.eaFX, pNewFX);
		}
	}

	return accum;
}

/// Destroy INFO and relinquish its memory.
void gfxHeadshotInfoDestroy( HeadshotInfo* info )
{
	gfxHeadshotInfoDestroyEx( info, true, true );
}

void gfxHeadshotInfoDestroyEx( HeadshotInfo* info, bool bFreeTexture, bool bRemoveActive )
{
	if( bRemoveActive ) {
		eaFindAndRemove( &headshotActiveList, info );
	}
	eaFindAndRemove( &headshotUnprocessedList, info );
	eaFindAndRemove( &headshotAlphaUnprocessedList, info );
	eaFindAndRemove( &headshotAnimatedList, info );
	{
		int actionIt;
		for( actionIt = 0; actionIt != eaSize( &headshotActionsThisFrame ); ++actionIt ) {
			if( headshotActionsThisFrame[ actionIt ]->info == info ) {
				free( headshotActionsThisFrame[ actionIt ]);
				eaRemove( &headshotActionsThisFrame, actionIt );
				break;
			}
		}
		for( actionIt = 0; actionIt != eaSize( &headshotAlphaActionsThisFrame ); ++actionIt ) {
			if( headshotAlphaActionsThisFrame[ actionIt ]->info == info ) {
				eaPush( &headshotTexturePool, headshotAlphaActionsThisFrame[ actionIt ]->srcBlackTex );
				eaPush( &headshotTexturePool, headshotAlphaActionsThisFrame[ actionIt ]->srcGrayTex );
				free( headshotAlphaActionsThisFrame[ actionIt ]);
				eaRemove( &headshotAlphaActionsThisFrame, actionIt );
				break;
			}
		}
	}
	
	if( bFreeTexture ) {
		gfxHeadshotFreeTexture(info->texture);
	}
	if( info->regionId >= 0 ) {
		eaiPush( &headshotFreeRegionIds, info->regionId );
	}
			
	switch( info->type ) {
		case HETY_COSTUME: case HETY_ANIMATED_COSTUME: {
			if (!gConf.bNewAnimationSystem)
			{
				if( info->costume.playerSkel ) {
					dynSeqRemoveBitFieldFeed( info->costume.playerSkel, &info->costume.bfg );
				}
			}
			if( info->costume.playerDrawSkel ) {
				dynDrawSkeletonFree( info->costume.playerDrawSkel );
			}
			if( info->costume.playerSkel ) {
				dynSkeletonFree( info->costume.playerSkel );
			}
			if( info->costume.fxMan ) {
				dynFxManDestroy( info->costume.fxMan );
			}
			if( info->costume.root ) {
				dynNodeFree( info->costume.root );
			}
			if ( info->costume.eaFX ) {
				eaDestroyStruct(&info->costume.eaFX, parse_PCFXTemp);
			}
		}

		xcase HETY_GROUP: case HETY_ANIMATED_GROUP: {
			eaDestroyStruct(&info->group.params, parse_GroupChildParameter);
		}

		xcase HETY_SCENE: {
		}

		xcase HETY_CONSTRUCTED_SCENE: {
		}

		xcase HETY_MODEL: {
		}

		xcase HETY_MATERIAL: {
		}

		xdefault: {
			FatalErrorf( "You must create free code for the headshot type, HeadshotType=%d", info->type );
		}
	}

	if( info->sky ) {
		free( info->sky );
	}
	
	if(info->esExtraInfo)
	{
		estrDestroy(&info->esExtraInfo);
	}
	
	free( info );
}

/// Capture a headshot of the SKEL/DRAW-SKEL in their current state
/// and save that into TEXTURE.
static void headshotCaptureSkeleton( const Vec3 camPos, const Vec3 camDir, HeadshotInfo* info )
{
	DynSkeleton* skel = info->costume.playerSkel;
	DynDrawSkeleton* drawSkel = info->costume.playerDrawSkel;
	DynFxManager* manager = info->costume.fxMan;
	WorldRegion* region;

	assert( info->texture && skel && drawSkel );

	region = gfxHeadshotBegin( info, camPos, camDir, NULL );

	if( !region )
		return;
	
	if( manager )
		dynFxManSetRegion( manager, region );

	dynFxManagerUpdate( info->costume.fxMan, 1 ); // 1 is the smallest unit of DYNFXTIME we can pass in
	gfxQueueSingleDynDrawSkeleton( drawSkel, region, true, true, NULL );
	FOR_EACH_IN_EARRAY(drawSkel->eaSubDrawSkeletons, DynDrawSkeleton, subDrawSkel)
		gfxQueueSingleDynDrawSkeleton( subDrawSkel, region, false, true, NULL );
	FOR_EACH_END;
	gfxFillDrawList( true, NULL );
	gfxHeadshotEnd();
}

/// Capture a "headshot" of GROUP in its current state, and save that
/// into TEXTURE.
///
/// Only works with object library pieces.
BasicTexture* gfxHeadshotCaptureGroup_dbg( 
					const char* headshotName, int width, int height, GroupDef* group,
					BasicTexture* background, Color bgColor, GfxHeadshotObjectCamType cam_type,
					GroupChildParameter **params, const char *sky, float near_plane, 
					bool enable_shadows, bool show_headshot_groups, 
					HeadshotNotifyBytesF notifyBytesF, UserData notifyBytesData
					MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );
	
	if( !group || group->name_uid >= 0 || gfxHeadshotDisable ) {
		gfxHeadshotAssertConsistantState();
		texGenFreeNextFrame( texture );
		return NULL;
	}

	{
		HeadshotInfo* accum = calloc( 1, sizeof( *accum ));
		accum->fname = caller_fname;
		accum->line = line;
		accum->texture = texture;
		accum->type = HETY_GROUP;
		accum->group.uid = group->name_uid;
		accum->group.nearPlane = near_plane;
		accum->group.enableShadows = enable_shadows;
		accum->group.showHeadshotGroups = show_headshot_groups;
		accum->background = background;
		setVec4( accum->bgTexCoords, 0, 0, 1, 1 );
		accum->bgColor = bgColor;
		accum->sky = strdup(sky);
		accum->notifyBytesF = notifyBytesF;
		accum->notifyBytesData = notifyBytesData;

		accum->group.camType = cam_type;

		if( eaiSize( &headshotFreeRegionIds )) {
			accum->regionId = eaiPop( &headshotFreeRegionIds );
		} else {
			accum->regionId = headshotRegionCounter++;
		}

		eaCopyStructs(&params, &accum->group.params, parse_GroupChildParameter);

		eaPush( &headshotUnprocessedList, accum );
		eaPush( &headshotActiveList, accum );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();

	return texture;
}

/// Capture a "headshot" of GROUP in its current state, and save that
/// into TEXTURE.  Will allow animating the camera.  Only one thing
/// can be animated at a time, for memory reasons.
///
/// Only works with object library pieces.
BasicTexture* gfxHeadshotCaptureAnimatedGroupScene_dbg(
		const char* headshotName, int width, int height, GroupDef* group, BasicTexture* background,
		Color bgColor, GroupChildParameter **params, const char *sky, float near_plane, bool enable_shadows, bool show_headshot_groups,
		HeadshotUpdateCameraF updateCameraF, UserData updateCameraData
		MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );
	
	if( !group || gfxHeadshotDisable ) {
		gfxHeadshotAssertConsistantState();
		return texture;
	}

	{
		HeadshotInfo* info = calloc( 1, sizeof( *info ));
		info->fname = caller_fname;
		info->line = line;
		info->texture = texture;
		info->type = HETY_ANIMATED_GROUP;
		info->group.uid = group->name_uid;
		info->group.nearPlane = near_plane;
		info->group.enableShadows = enable_shadows;
		info->group.showHeadshotGroups = show_headshot_groups;
		info->background = background;
		setVec4( info->bgTexCoords, 0, 0, 1, 1 );
		info->bgColor = bgColor;
		info->sky = strdup(sky);
		info->updateCameraF = updateCameraF;
		info->updateCameraData = updateCameraData;


		devassertmsg( eaSize(&headshotAnimatedList) < HEADSHOT_MAX_ANIMATED, "Too many animated headshots" );
		eaPush( &headshotAnimatedList, info );

	}
	
	gfxHeadshotAssertConsistantState();

	return texture;
}

/// See GFX-HEADSHOT-CAPTURE-GROUP.
///
/// This does the same thing, without registering a HEADSHOT-INFO.
void gfxHeadshotCaptureGroup1( HeadshotInfo* info, bool reopenTempEntry )
{
	GroupDef* group;
	WorldRegion *region;
	Vec3 camPos;
	Vec3 camDir;
	Vec4 pyr = { RAD( 30 ), RAD( -30 ), RAD( 0 ) };
	HeadshotOrthoParams *ortho_params = NULL;

	assert( info->group.uid < 0 );
	group = objectLibraryGetGroupDef(info->group.uid, true);

	assert( info->texture );
	assert( group );

	if( info->updateCameraF ) {
		info->updateCameraF( info->updateCameraData, NULL, group, camPos, camDir );
	} else if( info->group.camType == GFX_HEADSHOT_OBJECT_FROM_ABOVE ) {
		ortho_params = calloc(1, sizeof(HeadshotOrthoParams));
		ortho_params->world_w = group->bounds.max[0]-group->bounds.min[0];
		ortho_params->world_h = group->bounds.max[2]-group->bounds.min[2];
		ortho_params->far_plane = group->bounds.max[1] - group->bounds.min[1] + 5.f;
		ortho_params->near_plane = 0;
		copyVec3(group->bounds.mid, ortho_params->center);
		if (info->group.nearPlane > -10000)
		{
			ortho_params->center[1] = info->group.nearPlane;
		}
		else
		{
			ortho_params->center[1] = group->bounds.max[1];
		}

		if (ortho_params->world_w == 0 || ortho_params->world_h == 0)
		{
			Errorf("GroupDef %s (%d) has no bounds while taking headshot. Does the object have any geometry?", group->name_str, info->group.uid);
			MAX1(ortho_params->world_w, 1);
			MAX1(ortho_params->world_h, 1);
		}

		setVec3(ortho_params->pyr, 0.5f*PI, 0, PI);
	} else {
		gfxHeadshotCalcCamPosAndDir( camPos, camDir, group->bounds.min, group->bounds.max,
									 (float)info->texture->width / info->texture->height,
									 pyr, -1 );
	}

	if (reopenTempEntry)
	{
		TempGroupParams params = { 0 };
		extern int g_bDebugAllowCancelLoads;
		bool bSaved = g_bDebugAllowCancelLoads;

		region = gfxHeadshotLookupRegionForInfo( info, camPos );

		params.override_region_name = worldRegionGetRegionName(region);
		params.no_culling = true;
		params.disable_vertex_lighting = !info->group.enableShadows;
		params.params = info->group.params;
		params.no_sound = true;
		params.in_headshot = info->group.showHeadshotGroups;
		g_bDebugAllowCancelLoads = true; // Allow loading to fail so as not to stall the main thread
		groupInfoOverrideIntParameterValue = 0;
		worldAddTempGroup( group, unitmat, &params, true );
		groupInfoOverrideIntParameterValue = -1;
		g_bDebugAllowCancelLoads = bSaved;
	}

	region = gfxHeadshotBegin( info, camPos, camDir, ortho_params );
	SAFE_FREE(ortho_params);

	if( !region ) {
		return;
	}

	gfxFillDrawList( true, NULL );
	gfxHeadshotEnd();
}

/// Capture a "headshot" of the world at CAM-POS, looking at
/// CAM-LOOK-AT, and save that into TEXTURE.
///
/// WARNING: Do not call this function looking too far from where the
/// player currently is; this does not handle LODs at all.
BasicTexture* gfxHeadshotCaptureScene_dbg( const char* headshotName, int width, int height, Vec3 camPos, Vec3 camDir MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );
	
	if( gfxHeadshotDisable ) {
		gfxHeadshotAssertConsistantState();
		texGenFreeNextFrame( texture );
		return NULL;
	}

	{
		HeadshotInfo* accum = calloc( 1, sizeof( *accum ));
		accum->fname = caller_fname;
		accum->line = line;
		accum->texture = texture;
		accum->type = HETY_SCENE;
		copyVec3( camPos, accum->scene.camPos );
		copyVec3( camDir, accum->scene.camDir );
		accum->regionId = -1;

		eaPush( &headshotUnprocessedList, accum );
		eaPush( &headshotActiveList, accum );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();

	return texture;
}

/// See GFX-HEADSHOT-CAPTURE-SCENE.
///
/// This does the same thing, without registering a HEADSHOT-INFO.
void gfxHeadshotCaptureScene1( HeadshotInfo* info )
{
	assert( info->type == HETY_SCENE );

	if( !gfxHeadshotBegin( info, info->scene.camPos, info->scene.camDir, NULL )) {
		return;
	}
	gfxFillDrawList( true, NULL );
	gfxHeadshotEnd();
}


/// Capture a "headshot" of the world using CAM-CONTROLLER and save
/// that into TEXTURE.
///
/// WARNING: Do not modify CAM-CONTROLLER after calling this function.
/// The headshot uses the camera directly.
BasicTexture* gfxHeadshotCaptureConstructedScene_dbg(
		const char* headshotName, int width, int height, GfxCameraController* camController,
		BasicTexture* background, Vec4 bgTexCoords, Color bgColor, bool allowOutlines,
		HeadshotNotifyBytesF notifyBytesF, UserData notifyBytesData
		MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );
	
	if( gfxHeadshotDisable ) {
		gfxHeadshotAssertConsistantState();
		texGenFreeNextFrame( texture );
		return NULL;
	}

	{
		HeadshotInfo* accum = calloc( 1, sizeof( *accum ));
		accum->fname = caller_fname;
		accum->line = line;
		accum->texture = texture;
		accum->type = HETY_CONSTRUCTED_SCENE;
		accum->background = background;
		if( bgTexCoords ) {
			copyVec4( bgTexCoords, accum->bgTexCoords );
		} else {
			setVec4( accum->bgTexCoords, 0, 0, 1, 1 );
		}
		accum->bgColor = bgColor;
		accum->constructedScene.allowOutlines = allowOutlines;
		accum->constructedScene.camController = camController;
		accum->regionId = -1;
		accum->notifyBytesF = notifyBytesF;
		accum->notifyBytesData = notifyBytesData;

		eaPush( &headshotUnprocessedList, accum );
		eaPush( &headshotActiveList, accum );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();

	return texture;
}


/// See GFX-HEADSHOT-CAPTURE-CONSTRUCTED-SCENE.
///
/// This does the same thing, without registering a HEADSHOT-INFO.
void gfxHeadshotCaptureConstructedScene1( HeadshotInfo* info )
{
	assert( info->type == HETY_CONSTRUCTED_SCENE );

	if( !gfxHeadshotBegin( info, NULL, NULL, NULL )) {
		return;
	}
	gfxFillDrawList( true, NULL );
	gfxHeadshotEnd();
}

/// Capture a model into a headshot texture, optionally replace with a
/// material.
BasicTexture* gfxHeadshotCaptureModelEx_dbg( const char* headshotName, int width, int height, Model* model, Material* material, Color bgColor, BasicTexture **eaTextureSwaps MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );
	
	if( !model || gfxHeadshotDisable  ) {
		gfxHeadshotAssertConsistantState();
		texGenFreeNextFrame( texture );
		return NULL;
	}

	{
		HeadshotInfo* accum = calloc( 1, sizeof( *accum ));
		accum->fname = caller_fname;
		accum->line = line;
		accum->texture = texture;
		accum->type = HETY_MODEL;
		accum->regionId = -1;
		accum->model.material = material;
		accum->model.model = model;
		accum->model.eaTextureSwaps = eaTextureSwaps;
		accum->bgColor = bgColor;

		eaPush( &headshotUnprocessedList, accum );
		eaPush( &headshotActiveList, accum );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();

	return texture;
}

static const char * MATERIAL_CAPTURE_MODEL = "box_50_feet";

/// Capture a material into a headshot texture.
BasicTexture* gfxHeadshotCaptureMaterial_dbg( const char* headshotName, int width, int height, Material* material, Color bgColor MEM_DBG_PARMS )
{
	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );

	if( gfxHeadshotDisable  ) {
		gfxHeadshotAssertConsistantState();
		texGenFreeNextFrame( texture );
		return NULL;
	}

	{
		char destpath[MAX_PATH];
		HeadshotInfo* accum = calloc( 1, sizeof( *accum ));
		accum->fname = caller_fname;
		accum->line = line;
		accum->texture = texture;
		accum->type = HETY_MATERIAL;
		accum->regionId = -1;
		accum->model.material = material;
		accum->model.model = modelFind(MATERIAL_CAPTURE_MODEL, true, WL_FOR_UTIL);
		if (!accum->model.model)
		{
			ErrorFilenamef(MATERIAL_CAPTURE_MODEL, "Material capture needs system model %s.", 
				MATERIAL_CAPTURE_MODEL);
			return NULL;
		}
		accum->bgColor = bgColor;

		fileSpecialDir("screenshots", SAFESTR(destpath));
		strcat(destpath, "/");
		strcat(destpath, material->material_name);
		strcat(destpath, ".png");

		accum->notifyBytesF = gfxHeadshotNotifyBytesSaveFile;
		accum->notifyBytesData = strdup(destpath);

		eaPush( &headshotUnprocessedList, accum );
		eaPush( &headshotActiveList, accum );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();

	return texture;
}

/// See GFX-HEADSHOT-CAPTURE-MODEL.
///
/// This does the same thing, but without registering a HEADSHOT-INFO.
void gfxHeadshotCaptureModel1( HeadshotInfo* info )
{
	Vec3 camPos;
	Vec3 camDir;
	Vec4 pyr = { RAD( 30 ), RAD( -30 ), RAD( 0 ) };
	assert( info->type == HETY_MODEL || info->type == HETY_MATERIAL );

	if (info->type == HETY_MODEL)
	{
		gfxHeadshotCalcCamPosAndDir( camPos, camDir, info->model.model->min, info->model.model->max,
									 (float)info->texture->width / info->texture->height, pyr, -1 );
		if( !gfxHeadshotBegin( info, camPos, camDir, NULL )) {
			return;
		}
	}
	else
	{
		HeadshotOrthoParams ortho_settings;
		ortho_settings.world_w = info->model.model->max[0] - info->model.model->min[0];
		ortho_settings.world_h = info->model.model->max[2] - info->model.model->min[2];
		addVec3(info->model.model->min, info->model.model->max, ortho_settings.center);
		scaleVec3(ortho_settings.center, 0.5f, ortho_settings.center);
		ortho_settings.center[1] += 30.0f;
		ortho_settings.far_plane = (info->model.model->min[1] - ortho_settings.center[1]) * 2 + ortho_settings.center[1] - 100;
		ortho_settings.near_plane = (info->model.model->max[1] - ortho_settings.center[1]) * 2 + ortho_settings.center[1] + 100;
		setVec3(ortho_settings.pyr, 0.5f*PI, 0, PI);
		
		if( !gfxHeadshotBegin( info, camPos, camDir, &ortho_settings )) {
			return;
		}
	}

	if( info->model.material && !info->model.material->graphic_props.render_info ) {
		bool oldErrorOnNonPreloaded = gfx_state.debug.error_on_non_preloaded_materials;
		gfx_state.debug.error_on_non_preloaded_materials = false;

		// TODO: Does this need extra texture swaps and defines? -Cliff
		gfxMaterialsInitMaterial( info->model.material, true );
		gfx_state.debug.error_on_non_preloaded_materials = oldErrorOnNonPreloaded;
	}
	assert( !info->model.material || info->model.material->graphic_props.render_info );

	{
		SingleModelParams params = { 0 };
		copyVec3( onevec3, params.color );
		params.alpha = 255;
		params.eaNamedConstants = gfxMaterialStaticTintColorArray( onevec3 );
		copyVec3( onevec3, params.eaNamedConstants[ 0 ]->value );
		params.model = info->model.model;
		assert( params.model );		
		params.material_replace = info->model.material;
		params.eaTextureSwaps = info->model.eaTextureSwaps;
		params.override_visscale = HEADSHOT_VISSCALE;
		identityMat4( params.world_mat );
		gfxQueueSingleModelTinted( &params, -1 );
	}

	gfxHeadshotEnd();
}

/// Validate that width/height are one of the allowed sizes for normal
/// game runtime.
void gfxHeadshotValidateSizeForRuntime( int* width, int* height )
{
	if( gfxHeadshotDisableValidateSize ) {
		return;
	}

	if( *width == 128 && *height == 128 ) {
		return;
	}
	if( *width == 256 && *height == 256 ) {
		return;
	}
	if( *width == 512 && *height == 512 ) {
		return;
	}
	if( *width == 256 && *height == 512 ) {
		return;
	}
	if( *width == 512 && *height == 256 ) {
		return;
	}
	if( *width == 512 && *height == 1024 ) {
		return;
	}
	if( *width == 1024 && *height == 512 ) {
		return;
	}
	if( *width == 1024 && *height == 1024 ) {
		return;
	}

	// for saving "costumes" out -- should be infrequent
	if( *width == 300 && *height == 400 ) {
		return;
	}

	Errorf( "Game requested a headshot sized %d x %d.  It should not have.", *width, *height );
	*width = 512;
	*height = 512;
}

/// Validate that width/height are one of the allowed sizes for the
/// headshot server.
void gfxHeadshotValidateSizeForHeadshotServer( int* width, int* height )
{
	if( gfxHeadshotDisableValidateSize ) {
		return;
	}

	if(*width != *height
		|| !isPower2(*width)
		|| *width < 64
		|| *width > 1024)
	{
		int cursize = MIN(MAX(*width, *height), 1024);
		int size = 64;

		Errorf( "HeadshotServer requested headshot sized %d x %d.  Width and height should be the same and a power of two [64 - 1024].", *width, *height );

		while(size < cursize)
		{
			size <<= 1;
		}
		*width = *height = size;
	}
}

/// Allocate a headshot texture
BasicTexture* gfxHeadshotAllocateTexture( const char* texName, int width, int height )
{
	int it;
	for( it = 0; it != eaSize( &headshotTexturePool ); ++it ) {
		BasicTexture* tex = headshotTexturePool[ it ];
		if( tex->width == width && tex->height == height ) {
			eaRemoveFast( &headshotTexturePool, it );
			SAFE_FREE( tex->name );
			tex->name = strdup( texName );
			return tex;
		}
	}

	return (texAllocateScratch)( texName, width, height, WL_FOR_UI );
}

/// Reuse a compatible texture with texture if in the pool
void gfxHeadshotSwapWithCompatibleTexture( BasicTexture* tex )
{
	int it;
	
	for( it = 0; it != eaSize( &headshotTexturePool ); ++it ) {
		BasicTexture* poolTex = headshotTexturePool[ it ];
		// Only use another texture which matches the size AND has
		// video memory backing.
		if(   tex->width == poolTex->width && tex->height == poolTex->height
			  && SAFE_MEMBER( poolTex->loaded_data, tex_memory_use[ TEX_MEM_VIDEO ])) {
			rdrSwapTexHandles(gfx_state.currentDevice->rdr_device, tex->tex_handle, poolTex->tex_handle);

			eaRemoveFast( &headshotTexturePool, it );
			texGenFreeNextFrame( poolTex );
			return;
		}
	}
}

/// Capture a headshot of COSTUME and save it into a DEST-FILE with
/// size WIDTH x HEIGHT.
///
/// Other args act as in GFX-HEADSHOT-CAPTURE-COSTUME.
void gfxHeadshotCaptureCostumeAndSave_dbg(
		const char* destFile, int width, int height, WLCostume* costume,
		BasicTexture* background, const char* framename, Color bgColor, bool forceBodyshot,
		DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance, float fovY, const char* sky, HeadshotNotifyF notifyF, UserData notifyData,
		char **ppExtraInfo MEM_DBG_PARMS )
{
	BasicTexture* texture;

	if( !destFile || !costume || gfxHeadshotDisable  ) {
		gfxHeadshotAssertConsistantState();
		return;
	}

	if( gfxHeadshotIsHeadshotServer ) {
		gfxHeadshotValidateSizeForHeadshotServer( &width, &height );
	} else {
		gfxHeadshotValidateSizeForRuntime( &width, &height );
	}
	
	texture = gfxHeadshotAllocateTexture( getFileNameConst( destFile ), width, height );
	
	{
		HeadshotInfo* info = gfxHeadshotInfoCostumeCreate( texture, costume, background, framename, bgColor, forceBodyshot, bfg, pcAnimKeyword, pchStance, sky, NULL, NULL, 0.1, ppExtraInfo, fovY, NULL, NULL, NULL MEM_DBG_PARMS_CALL );
		if( info == NULL ) {
			gfxHeadshotAssertConsistantState();
			return;
		}
		
		info->notifyBytesF = gfxHeadshotNotifyBytesSaveFile;
		info->notifyBytesData = strdup( destFile );
		info->notifyF = notifyF;
		info->notifyData = notifyData;

		eaPush( &headshotUnprocessedList, info );
		eaPush( &headshotActiveList, info );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();
}

/// Capture a headshot of COSTUME and save it into a DEST-FILE with
/// size WIDTH x HEIGHT.
///
/// Other args act as in GFX-HEADSHOT-CAPTURE-COSTUME-SCENE.
void gfxHeadshotCaptureCostumeSceneAndSave_dbg(
		const char* destFile, int width, int height, WLCostume* costume,
		BasicTexture* background, Color bgColor, DynBitFieldGroup* bfg, const char *pcAnimKeyword, const char* pchStance, 
		Vec3 camPos, Vec3 camDir, float fovY, const char* sky,
		HeadshotNotifyF notifyF, UserData notifyData, float animDelta, bool forceBodyshot,
		char **ppExtraInfo MEM_DBG_PARMS )
{
	BasicTexture* texture;

	PERFINFO_AUTO_START_FUNC();

	if( !destFile || !costume || gfxHeadshotDisable  ) {
		gfxHeadshotAssertConsistantState();
		PERFINFO_AUTO_STOP();
		return;
	}

	if( gfxHeadshotIsHeadshotServer ) {
		gfxHeadshotValidateSizeForHeadshotServer( &width, &height );
	} else {
		gfxHeadshotValidateSizeForRuntime( &width, &height );
	}
	
	texture = gfxHeadshotAllocateTexture( getFileNameConst( destFile ), width, height );
	
	{
		HeadshotInfo* info = gfxHeadshotInfoCostumeCreate( texture, costume, background, NULL, bgColor, forceBodyshot, bfg, pcAnimKeyword, pchStance, sky, camPos, camDir, animDelta, ppExtraInfo, fovY, NULL, NULL, NULL MEM_DBG_PARMS_CALL );
		if( info == NULL ) {
			gfxHeadshotAssertConsistantState();
			PERFINFO_AUTO_STOP();
			return;
		}

		info->notifyBytesF = gfxHeadshotNotifyBytesSaveFile;
		info->notifyBytesData = strdup( destFile );
		info->notifyF = notifyF;
		info->notifyData = notifyData;

		eaPush( &headshotUnprocessedList, info );
		eaPush( &headshotActiveList, info );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();
	PERFINFO_AUTO_STOP();
}

/// Capture a headshot of GROUP and save it into a DEST-FILE with
/// size WIDTH x HEIGHT.
///
/// Other args act as in GFX-HEADSHOT-CAPTURE-GROUP.
void gfxHeadshotCaptureGroupAndSave_dbg(
		const char* destFile, int width, int height, GroupDef* group,
		BasicTexture* background, Color bgColor, HeadshotNotifyF notifyF,
		UserData notifyData MEM_DBG_PARMS )
{
	BasicTexture* texture;

	if( !destFile || !group || gfxHeadshotDisable  ) {
		gfxHeadshotAssertConsistantState();
		return;
	}

	if( gfxHeadshotIsHeadshotServer ) {
		gfxHeadshotValidateSizeForHeadshotServer( &width, &height );
	} else {
		gfxHeadshotValidateSizeForRuntime( &width, &height );
	}
	
	texture = gfxHeadshotAllocateTexture( getFileNameConst( destFile ), width, height );

	{
		HeadshotInfo* info = calloc( 1, sizeof( *info ));
		info->fname = caller_fname;
		info->line = line;
		info->texture = texture;
		info->notifyBytesF = gfxHeadshotNotifyBytesSaveFile;
		info->notifyBytesData = strdup( destFile );
		info->notifyF = notifyF;
		info->notifyData = notifyData;
		info->type = HETY_GROUP;
		info->group.uid = group->name_uid;
		info->background = background;
		setVec4( info->bgTexCoords, 0, 0, 1, 1 );
		info->bgColor = bgColor;

		if( eaiSize( &headshotFreeRegionIds )) {
			info->regionId = eaiPop( &headshotFreeRegionIds );
		} else {
			info->regionId = headshotRegionCounter++;
		}

		eaPush( &headshotUnprocessedList, info );
		eaPush( &headshotActiveList, info );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();
}

/// Capture a headshot of a scene and save it into DEST-FILE with size
/// WIDTH x HEIGHT.
///
/// Other args act as in GFX-HEADSHOT-CAPTURE-SCENE.
void gfxHeadshotCaptureSceneAndSave_dbg( const char* destFile, int width, int height, Vec3 camPos, Vec3 camDir,
										 HeadshotNotifyF notifyF, UserData notifyData MEM_DBG_PARMS )
{
	BasicTexture* texture;

	if( !destFile || gfxHeadshotDisable  ) {
		gfxHeadshotAssertConsistantState();
		return;
	}

	if( gfxHeadshotIsHeadshotServer ) {
		gfxHeadshotValidateSizeForHeadshotServer( &width, &height );
	} else {
		gfxHeadshotValidateSizeForRuntime( &width, &height );
	}
	
	texture = gfxHeadshotAllocateTexture( getFileNameConst( destFile ), width, height );

	{
		HeadshotInfo* accum = calloc( 1, sizeof( *accum ));
		accum->fname = caller_fname;
		accum->line = line;
		accum->texture = texture;
		accum->type = HETY_SCENE;
		copyVec3( camPos, accum->scene.camPos );
		copyVec3( camDir, accum->scene.camDir );
		accum->regionId = -1;
		
		accum->notifyBytesF = gfxHeadshotNotifyBytesSaveFile;
		accum->notifyBytesData = strdup( destFile );
		accum->notifyF = notifyF;
		accum->notifyData = notifyData;

		eaPush( &headshotUnprocessedList, accum );
		eaPush( &headshotActiveList, accum );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();
}

/// Capture a headshot of the world using CAM-CONTROLLER and save it
/// into DEST-FILE with size WIDTH x HEIGHT.
///
/// Other args act as in GFX-HEADSHOT-CAPTURE-CONSTRUCTED-SCENE.
void gfxHeadshotCaptureConstructedSceneAndSave_dbg( const char* destFile, int width, int height, 
													GfxCameraController* camController, bool allowOutlines,
													HeadshotNotifyF notifyF, UserData notifyData
													MEM_DBG_PARMS )
{
	BasicTexture* texture;
	
	if( !destFile || gfxHeadshotDisable  ) {
		return;
	}

	if( gfxHeadshotIsHeadshotServer ) {
		gfxHeadshotValidateSizeForHeadshotServer( &width, &height );
	} else {
		gfxHeadshotValidateSizeForRuntime( &width, &height );
	}
	
	texture = gfxHeadshotAllocateTexture( getFileNameConst( destFile ), width, height );

	{
		HeadshotInfo* accum = calloc( 1, sizeof( *accum ));
		accum->fname = caller_fname;
		accum->line = line;
		accum->texture = texture;
		accum->type = HETY_CONSTRUCTED_SCENE;
		accum->constructedScene.allowOutlines = allowOutlines;
		accum->constructedScene.camController = camController;
		accum->regionId = -1;

		accum->notifyBytesF = gfxHeadshotNotifyBytesSaveFile;
		accum->notifyBytesData = strdup( destFile );
		accum->notifyF = notifyF;
		accum->notifyData = notifyData;

		eaPush( &headshotUnprocessedList, accum );
		eaPush( &headshotActiveList, accum );
		headshotRequestedFramesAgo = 0;
	}
}

/// Checks to see if the given TEXTURE is waiting to be processed.
bool gfxHeadshotIsFinishedCostume( BasicTexture* texture )
{
	int it;

	if ( texture ) {

		for( it = 0; it != eaSize( &headshotUnprocessedList ); ++it ) {
			HeadshotInfo* info = headshotUnprocessedList[ it ];

			if ( info->texture == texture ) {
				return false;
			}
		}

		for( it = 0; it != eaSize( &headshotAlphaUnprocessedList ); ++it ) {
			HeadshotInfo* info = headshotAlphaUnprocessedList[ it ];

			if ( info->texture == texture ) {
				return false;
			}
		}

	}

	return true;
}

// release the headshot but bool to free texture
void gfxHeadshotReleaseFreeTexture( BasicTexture* texture, bool bFreeTexture )
{
	// If the texture is found in the cleanup list, free it and exit
	{
		int i = eaFind(&headshotTextureCleanupList, texture);
		if (i >= 0)
		{
			gfxHeadshotFreeTexture(headshotTextureCleanupList[i]);
			eaRemoveFast(&headshotTextureCleanupList, i);
			eaRemoveFast(&headshotTextureCleanupListFname, i);
			eaiRemoveFast(&headshotTextureCleanupListLinenum, i);
			gfxHeadshotAssertConsistantState();
			return;
		}
	}

	{
		int it;
		for( it = 0; it != eaSize( &headshotAnimatedList ); ++it ) {
			HeadshotInfo* info = headshotAnimatedList[ it ];

			if( info->texture == texture ) {
				gfxHeadshotInfoDestroyEx( info, bFreeTexture, true );
				gfxHeadshotAssertConsistantState();
				return;
			}
		}
	}

	{
		int it;
		for( it = 0; it != eaSize( &headshotActiveList ); ++it ) {
			HeadshotInfo* info = headshotActiveList[ it ];

			if( info->texture == texture ) {
				gfxHeadshotInfoDestroyEx( info, bFreeTexture, true );
				gfxHeadshotAssertConsistantState();
				return;
			}
		}
	}

	Errorf( "Trying to release a headshot that is not active!" );
	gfxHeadshotAssertConsistantState();
}

/// Release a previously requested headshot.
void gfxHeadshotRelease( BasicTexture* texture )
{
	if( !texture ) {
		return;
	}
	
	gfxHeadshotReleaseFreeTexture(texture, true);
	gfxHeadshotAssertConsistantState();
}

// Release all headshots
// If bOnlyUI is true, then only release headshots marked as bForUI. This does not release textures.
// Headshots released this way must have their textures released within 3 frames.
void gfxHeadshotReleaseAll(bool bOnlyUI)
{
	int i;
	for(i = eaSize(&headshotAnimatedList)-1; i >= 0; i--) {
		HeadshotInfo* pInfo = headshotAnimatedList[i];

		if (!bOnlyUI || pInfo->bForUI) {
			eaPush(&headshotTextureCleanupList, pInfo->texture);
			eaPush(&headshotTextureCleanupListFname, pInfo->fname);
			eaiPush(&headshotTextureCleanupListLinenum, pInfo->line);
			gfxHeadshotInfoDestroyEx(pInfo, false, true);
		}
	}
	for(i = eaSize(&headshotActiveList)-1; i >= 0; i--) {
		HeadshotInfo* pInfo = headshotActiveList[i];
		
		if (!bOnlyUI || pInfo->bForUI) {
			eaPush(&headshotTextureCleanupList, pInfo->texture);
			eaPush(&headshotTextureCleanupListFname, pInfo->fname);
			eaiPush(&headshotTextureCleanupListLinenum, pInfo->line);
			gfxHeadshotInfoDestroyEx(pInfo, false, true);
		}
	}
	if (eaSize(&headshotTextureCleanupList))
	{
		headshotFramesUntilTextureCleanup = HEADSHOT_TEXTURE_CLEANUP_WAIT_FRAMES;
		gfxHeadshotAssertConsistantState();
	}
}

/// Refresh all headshots.  Callback incase something huge changes,
/// like the device got lost.
void gfxHeadshotRefreshAll( void )
{
	eaClear( &headshotUnprocessedList );
	eaClear( &headshotAlphaUnprocessedList );

	{
		int it;
		for( it = 0; it != eaSize( &headshotActiveList ); ++it ) {
			HeadshotInfo* info = headshotActiveList[ it ];
			info->badCount = 0;
			eaPush( &headshotUnprocessedList, info );
		}
	}
	headshotRequestedFramesAgo = 0;
}

static bool isZeroVec3( Vec3 vec )
{
	return vec[0] == 0 && vec[1] == 0 && vec[2] == 0;
}

/// Calculate the camera pos and direction for a given headshot info.
static void gfxHeadshotSkeletonCalcCamPosAndDir( Vec3 outCamPos, Vec3 outCamDir, SA_PARAM_NN_VALID HeadshotInfo* info )
{
	const DynNode* targetNode;
	HeadShotFrame* frame = NULL;

	// First try to find a headshot frame
	if (info->costume.framename && info->costume.playerSkel)
	{
		WLCostume* costume = GET_REF(info->costume.playerSkel->hCostume);
		if (costume)
		{
			SkelInfo* skelInfo = GET_REF(costume->hSkelInfo);
			if (skelInfo)
			{
				SkelHeadshotInfo* skelHeadshotInfo = GET_REF(skelInfo->hHeadshotInfo);
				if (skelHeadshotInfo)
				{
					// Replace this with an earray search for the (stringcached) string for the type of headshot frame you want.
					frame = NULL;
					FOR_EACH_IN_EARRAY(skelHeadshotInfo->eaHeadShotFrame, HeadShotFrame, pFrame)
					{
						if (info->costume.framename == pFrame->pcFrameName)
						{
							frame = pFrame;
							break;
						}
						else if (!frame)
						{
							if (stricmp(pFrame->pcFrameName, "Default")==0)
							{
								frame = pFrame;
							}
						}
					}
					FOR_EACH_END;
				}
			}
		}
	}

	if( !isZeroVec3( gfxHeadshotOverrideCamPos ) || !isZeroVec3( gfxHeadshotOverrideCamDir )) {
		copyVec3( gfxHeadshotOverrideCamPos, outCamPos );
		copyVec3( gfxHeadshotOverrideCamDir, outCamDir );
	} else if ( info->updateCameraF ) {
		info->updateCameraF( info->updateCameraData, info->costume.playerSkel, NULL, outCamPos, outCamDir );
	} else if (frame) {
		findCameraPosFromHeadShotFrame(frame, info->costume.playerSkel, (info->fovY > 0 ? info->fovY : DEFAULT_FOV), (float)info->texture->width / info->texture->height, outCamPos);
		copyVec3(frame->vCameraDirection, outCamDir);
	} else if( (targetNode = dynSkeletonFindNode( info->costume.playerSkel, "HeadShot" )) && !info->costume.forceBodyshot && !info->costume.useOverrideCamera ) {
		Vec3 worldPos;
		ANALYSIS_ASSUME(targetNode);
		dynNodeGetWorldSpacePos( targetNode, worldPos );

		copyVec3( worldPos, outCamPos );
		setVec3( outCamDir, 0, 0, -1 );
	} else if( info->costume.useOverrideCamera ) {
		copyVec3( info->costume.overrideCamPos, outCamPos );
		copyVec3( info->costume.overrideCamDir, outCamDir );
	} else {
		Vec3 skelMin;
		Vec3 skelMax;
		Vec4 pyr = { RAD( 30 ), RAD( 0 ), RAD( 0 ) };
		dynSkeletonGetExpensiveExtents( info->costume.playerSkel, skelMin, skelMax, 0 );

		gfxHeadshotCalcCamPosAndDir( outCamPos, outCamDir, skelMin, skelMax,
									 (float)info->texture->width / info->texture->height,
									 pyr, info->fovY );
	}

	normalVec3(outCamDir);
}

/// Calculate the camera pos and direction for a given bounding box.
static void gfxHeadshotCalcCamPosAndDir(
		Vec3 outCamPos, Vec3 outCamDir, const Vec3 min, const Vec3 max,
		float aspectRatio, const Vec3 pyr, float fovY )
{
	float halfWidth = (max[ 0 ] - min[ 0 ]) / 2;
	float halfHeight = (max[ 1 ] - min[ 1 ]) / 2;
	float actualFOV = (fovY > 0 ? fovY : DEFAULT_FOV);
	float htan = tanf( 2 * fatan( aspectRatio * ftan( RAD( actualFOV ) / 2 ))); 
	float vtan = tanf( RAD( actualFOV ) / 2 );
	Mat3 mat;
	Vec3 tempDelta;
	Vec3 delta = { 0, 0, ((max[ 2 ] - min[ 2 ]) / 2
						   + max( halfWidth / htan, halfHeight / vtan ))};
	
	createMat3YPR( mat, pyr );
	mulVecMat3( delta, mat, tempDelta );
	copyVec3( tempDelta, delta );
	scaleVec3( delta, 1.5, delta );
	
	setVec3( outCamPos,
			 (min[ 0 ] + max[ 0 ]) / 2,
			 (min[ 1 ] + max[ 1 ]) / 2,
			 (min[ 2 ] + max[ 2 ]) / 2 );
	addVec3( outCamPos, delta, outCamPos );
	copyVec3( delta, outCamDir );
	scaleVec3( outCamDir, -1, outCamDir );
}

/// Callback for Costume dictionary changes.
///
/// This is needed in case the player changes costumes or the costume
/// updates keep getting sent.
static void gfxHeadshotCostumeDictChanged(
		enumResourceEventType eType, const char *pDictName, const char *pRefData,
		Referent pReferent, void *pUserData )
{
	WLCostume* costume = (WLCostume*)pReferent;
	
	switch( eType ) {
		case RESEVENT_RESOURCE_ADDED: case RESEVENT_RESOURCE_REMOVED:
		case RESEVENT_RESOURCE_MODIFIED: {
			int it;

			for( it = 0; it != eaSize( &headshotActiveList ); ++it ) {
				HeadshotInfo* info = headshotActiveList[ it ];
				if( info->type != HETY_COSTUME ) {
					continue;
				}

				if( info->costume.costumeName == pRefData ) {
					info->needsRecreate = true;
				}
			}

			for( it = 0; it != eaSize( &headshotAnimatedList ); ++it ) {
				HeadshotInfo* info = headshotAnimatedList[ it ];
				if( info->type != HETY_ANIMATED_COSTUME ) {
					continue;
				}

				if( info->costume.costumeName == pRefData ) {
					info->needsRecreate = true;
				}
			}
		}
	}
}

static int headshotDebugStyle = -1;
AUTO_CMD_INT( headshotDebugStyle, headshotDebugStyle );

static int headshotDebugStyle2 = 0;
AUTO_CMD_INT( headshotDebugStyle2, headshotDebugStyle2 );

static int headshotDebugLastStyle = -1;
static int headshotDebugLastStyle2 = -1;

static BasicTexture* headshotDebugTexture;

static void gfxHeadshotWritten( const char* name )
{
	Alertf( "Headshot %s was written.", name );
}

void gfxHeadshotDebugOncePerFrame(WLCostume* playerCostume, float realTimeElapsed)
{
	/* Disabling headshot debugging.  It may code rot a bit, but it's
	   now easy to use headshots from other parts of the code.
	   
	if( headshotDebugStyle < 0 )
		return;

	PERFINFO_AUTO_START_FUNC_PIX();

	if( !headshotDebugTexture ) {
		gfxHeadshotDebugTexture(); //< to force the headshot texture to be loaded
		texDemandLoad( headshotDebugTexture, 0.f, TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY, white_tex);
	}

	if(   headshotDebugLastStyle != headshotDebugStyle
		  && headshotDebugLastStyle > 0
		  && headshotDebugLastStyle <= 8 ) {
		gfxHeadshotRelease( headshotDebugTexture );
	}

	if( headshotDebugStyle == 0 ) {
		gfxHeadshotCaptureAnimatedCostume( headshotDebugTexture, playerCostume, realTimeElapsed, NULL, NULL, ColorBlack, false, NULL, -1, NULL );
	} else if( headshotDebugLastStyle != headshotDebugStyle || headshotDebugStyle2 != headshotDebugLastStyle2 ) {
		switch( headshotDebugStyle ) {
			case 1: {
				char buffer[ 128 ];
				GroupDef* group;
			
				sprintf( buffer, "mansion_tapestries_style_%02d", headshotDebugStyle2 + 1 );
				group = objectLibraryGetGroupDefByName( buffer, true );
				if( group )
					gfxHeadshotCaptureGroup( headshotDebugTexture, group, NULL, ColorTransparent );
			} break;

			case 2: {
				char buffer[ 128 ];
				GroupDef* group;

				sprintf( buffer, "crate_weapons_%02d", headshotDebugStyle2 + 1 );
				group = objectLibraryGetGroupDefByName( buffer, true );
				if( group )
					gfxHeadshotCaptureGroup( headshotDebugTexture, group, NULL, ColorBlack );
			} break;

			case 3: {
				char buffer[ 128 ];
				GroupDef* group;

				sprintf( buffer, "vehicle_military_ATV_%02d", headshotDebugStyle2 + 1 );
				group = objectLibraryGetGroupDefByName( buffer, true );
				if( group )
					gfxHeadshotCaptureGroup( headshotDebugTexture, group, NULL, ColorBlack );
			} break;

			case 4: {
				char buffer[ 128 ];
				GroupDef* group;
			
				sprintf( buffer, "village_crystal_ball_%02d", headshotDebugStyle2 + 1 );
				group = objectLibraryGetGroupDefByName( buffer, true );
				if( group )
					gfxHeadshotCaptureGroup( headshotDebugTexture, group, NULL, ColorBlack );
			} break;

			case 5: {
				char buffer[ 128 ];
				GroupDef* group;
			
				sprintf( buffer, "citytech_skytram_car_%02d", headshotDebugStyle2 + 1 );
				group = objectLibraryGetGroupDefByName( buffer, true );
				if( group )
					gfxHeadshotCaptureGroup( headshotDebugTexture, group, NULL, ColorBlack );
			} break;

			case 6: {
				Vec3 camPos = { 92, 18.05, 329.67 };
				//Vec3 camYPR = { 8.85, -79.31, 0 };
				Vec3 camDir = { 0.9266, -0.1698, -0.3355 };

				gfxHeadshotCaptureScene( headshotDebugTexture, camPos, camDir );
			} break;

			case 7: {
				gfxHeadshotCaptureConstructedScene( headshotDebugTexture, gfx_state.currentCameraController, NULL, NULL, ColorBlack, false, NULL, NULL );
			} break;

			case 8: {
				Vec3 pos = { 0, 10, 0 };
				Vec3 dir = { 0, -1, 0 };
				gfxHeadshotCaptureCostumeScene( headshotDebugTexture, playerCostume, NULL, ColorBlack, NULL, pos, dir, 0.1f, -1 );
			} break;

			case 10: {
			gfxHeadshotCaptureCostumeAndSave( "HeadshotDebug.tga", 256, 256, playerCostume, NULL, NULL, ColorBlack, false, NULL, -1, gfxHeadshotWritten, "HeadshotDebug.jpg", NULL );
			} break;
		}
	}

	headshotDebugLastStyle = headshotDebugStyle;
	headshotDebugLastStyle2 = headshotDebugStyle2;

	PERFINFO_AUTO_STOP_FUNC_PIX();
	*/
}

BasicTexture* gfxHeadshotDebugTexture( void )
{
	if( !headshotDebugTexture )
	{
		headshotDebugTexture = gfxHeadshotAllocateTexture( "HeadshotDebug", 256, 256 );
	}
	return headshotDebugTexture;
}

AUTO_COMMAND ACMD_COMMANDLINE ACMD_NAME(gfxHeadshotServerMode);
void gfxHeadshotServerMode(int ignored)
{
	gfxHeadshotIsHeadshotServer = true;
	headshotMaxPerFrameInternal = 5;

	gfx_state.debug.no_nalz_warnings = true;
	gfx_state.allow_preload_materials = false;
	gfx_state.settings.maxFps = 800;
	gfx_state.settings.maxInactiveFps = 800;
	globCmdParse( "shadows 0" );
	globCmdParse( "rendersize 4 4" );
	globCmdParse( "disableWind 1" );
}

/// Simulate a random texture becoming corrupt by being swapped with
/// some other texture.
AUTO_COMMAND;
void gfxHeadshotCorruptNextTexture(void)
{
	if( eaSize( &headshotActiveList ) == 0 ) {
		return;
	} else {
		int rand = randomIntRange(0, eaSize(&headshotActiveList) - 1);
		HeadshotInfo* info = headshotActiveList[ rand ];

		if( info->texture ) {
			memcpy( info->texture, texFindAndFlag("white", true, WL_FOR_UTIL), sizeof( *info->texture ));			
		}
	}
}

void gfxHeadshotForceAnimatedRecreation(void){
	int it;
	for ( it = eaSize(&headshotAnimatedList) - 1; it >= 0; it-- ) {
		headshotAnimatedList[it]->needsRecreate = true;
	}
}


// ----------------------------------------------------------------------
// Headshots for binning
// ----------------------------------------------------------------------

typedef struct BakedMaterialBinningAction BakedMaterialBinningAction;

typedef struct BakedMaterialBinningTexture {
	BasicTexture *tex;
	char *fileName;
	BakedMaterialBinningAction *action;
} BakedMaterialBinningTexture;

typedef struct BakedMaterialBinningAction {
	bool ready;
	BakedMaterialBinningTexture **eaTextures;
} BakedMaterialBinningAction;

BakedMaterialBinningAction *gfxHeadshotCreateBinningAction(void) {
	return calloc(1, sizeof(BakedMaterialBinningAction));
}

void gfxHeadshotNotifyBytesSaveFileAndContinueBinning(void* extraData, U8* data, int width, int height, char **ppExtraInfo ) {

	BakedMaterialBinningTexture *tex = (BakedMaterialBinningTexture*)extraData;
	BakedMaterialBinningAction *action = tex->action;
	int i;

	gfxHeadshotNotifyBytesSaveFile(strdup(tex->fileName), data, width, height, ppExtraInfo);

	// Find this in the list of actions and remove it.
	for(i = 0; i < eaSize(&action->eaTextures); i++) {
		if(action->eaTextures[i] == tex) {
			eaRemoveFast(&action->eaTextures, i);
			free(tex->fileName);
			free(tex);
			break;
		}
	}
}

bool gfxHeadshotBinningActionHasTexturesLeft(BakedMaterialBinningAction *action) {
	return eaSize(&action->eaTextures);
}

/// Capture a material into a headshot texture at binning time, where there is an expectation to block waiting for
/// textures to process.
BasicTexture* gfxHeadshotCaptureMaterialForBinning_dbg(
	const char* headshotName, const char *prefix, const char *uniqueName, int width, int height,
	Material* material, TextureSwap **eaTextureSwaps, Color bgColor, BakedMaterialBinningAction *action,
	char **finalPath MEM_DBG_PARMS) {

	BasicTexture* texture;

	gfxHeadshotValidateSizeForRuntime( &width, &height );
	texture = gfxHeadshotAllocateTexture( headshotName, width, height );

	if( gfxHeadshotDisable  ) {
		gfxHeadshotAssertConsistantState();
		texGenFreeNextFrame( texture );
		return NULL;
	}

	{
		BakedMaterialBinningTexture *mkbt = calloc(1, sizeof(BakedMaterialBinningTexture));
		char destpath[MAX_PATH];
		HeadshotInfo* accum = calloc( 1, sizeof( *accum ));
		int i;

		accum->fname = caller_fname;
		accum->line = line;
		accum->texture = texture;
		accum->type = HETY_MATERIAL;
		accum->regionId = -1;
		accum->model.material = material;

		for(i = 0; i < eaSize(&eaTextureSwaps); i++) {
			eaPush(&(accum->model.eaTextureSwaps), texFind(eaTextureSwaps[i]->orig_name, true));
			eaPush(&(accum->model.eaTextureSwaps), texFind(eaTextureSwaps[i]->replace_name, true));
		}

		accum->model.model = modelFind(MATERIAL_CAPTURE_MODEL, true, WL_FOR_UTIL);
		if (!accum->model.model)
		{
			ErrorFilenamef(MATERIAL_CAPTURE_MODEL, "Material capture needs system model %s.",
						   MATERIAL_CAPTURE_MODEL);
			return NULL;
		}
		accum->bgColor = bgColor;

		fileSpecialDir("screenshots", SAFESTR(destpath));
		strcat(destpath, "/bakedmaterial_");
		strcat(destpath, prefix);
		strcat(destpath, "_");
		strcat(destpath, uniqueName);
		strcat(destpath, ".png");
		estrPrintf(finalPath, "%s", destpath);

		mkbt->fileName = strdup(destpath);
		mkbt->tex = texture;
		mkbt->action = action;
		eaPush(&action->eaTextures, mkbt);

		accum->notifyBytesF = gfxHeadshotNotifyBytesSaveFileAndContinueBinning;
		accum->notifyBytesData = mkbt;

		eaPush( &headshotUnprocessedList, accum );
		eaPush( &headshotActiveList, accum );
		headshotRequestedFramesAgo = 0;
	}
	gfxHeadshotAssertConsistantState();

	return texture;
}
