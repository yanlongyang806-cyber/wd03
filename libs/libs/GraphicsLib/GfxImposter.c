#include "GfxImposter.h"

#include "GfxAuxDevice.h"
#include "GfxDrawFrame.h"
#include "GfxSky.h"
#include "GfxDebug.h"
#include "GfxSurface.h"
#include "GfxTextures.h"
#include "GfxDynamics.h"
#include "GfxWorld.h"
#include "GraphicsLibPrivate.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "dynDraw.h"
#include "dynSkeleton.h"
#include "dynFxManager.h"
#include "tga.h"
#include "wlCostume.h"
#include "quat.h"
#include "memlog.h"

AUTO_RUN_ANON(memBudgetAddMapping( __FILE__, BUDGET_EngineMisc ););

AUTO_CMD_INT(gfx_state.debug.bodysockTexAA, danimBodysockTexAA);

#define IMPOSTER_SKY_NAME "imposter_sky"

#define MAX_ATLASES_BEFORE_ERROR 6
#define NUM_ATLASES_KEEP_AROUND 4
#define NUM_EMPTY_FRAMES_BEFORE_DELETION 10

GfxImposterDebug gfxImposterDebug = {0};



typedef struct GfxImposterAtlasSection
{
	S32		iRefCount;
} GfxImposterAtlasSection;

typedef struct GfxImposterAtlasSpecs
{
	S32		iSectionResW;
	S32		iSectionResH;
	S32		iMaxSurfaceResW;
	S32		iNumSections;
} GfxImposterAtlasSpecs;

typedef struct GfxImposterAtlas
{
	int		iAtlasIndex;
	GfxImposterAtlasSpecs specs;
	int iRows;
	int iCols;
	int iUsedSections;
	int iNumFramesEmpty;
	GfxImposterAtlasSection* pSections;
	RdrSurface* pRdrSurface;
	BasicTexture* pTexture;
} GfxImposterAtlas;

typedef struct GfxLight GfxLight;

static void gfxImposterAtlasFree(GfxImposterAtlas* pAtlas);


GfxImposterAtlas* gfxImposterAtlasCreate(const GfxImposterAtlasSpecs* pSpecs, S32 iAtlasIndex)
{
	GfxImposterAtlas* pAtlas = calloc(sizeof(GfxImposterAtlas), 1);
	memcpy(&pAtlas->specs, pSpecs, sizeof(GfxImposterAtlasSpecs));
	pAtlas->iAtlasIndex = iAtlasIndex;

	assert(isPower2(pSpecs->iMaxSurfaceResW));
	assert(isPower2(pSpecs->iNumSections));
	assert(isPower2(pSpecs->iSectionResH));
	assert(isPower2(pSpecs->iSectionResW));
	assert(pSpecs->iSectionResW <= pSpecs->iMaxSurfaceResW);


	pAtlas->iRows = 1;
	while ( ( pSpecs->iNumSections / pAtlas->iRows ) * pSpecs->iSectionResW > pSpecs->iMaxSurfaceResW )
	{
		pAtlas->iRows *= 2;
		assert(pAtlas->iRows <= pSpecs->iNumSections); // something is broken, the individual section width should be less than the max surface width...
	}
	pAtlas->iCols = pSpecs->iNumSections / pAtlas->iRows;

	assert(pAtlas->iRows * pAtlas->iCols == pSpecs->iNumSections);

	pAtlas->pSections = calloc(sizeof(GfxImposterAtlasSection), pSpecs->iNumSections);

	{
		RdrSurfaceParams surfaceParams = {0};
		char cSurfaceName[64];

		sprintf(cSurfaceName, "GfxImposterAtlas_%d", iAtlasIndex);
		
		surfaceParams.name = allocAddString(cSurfaceName);
		rdrSurfaceParamSetSizeSafe(&surfaceParams, pAtlas->iCols * pSpecs->iSectionResW, 
			pAtlas->iRows * pSpecs->iSectionResH);
		surfaceParams.desired_multisample_level = 1;
		surfaceParams.required_multisample_level = 1;
		surfaceParams.depth_bits = 24;
		surfaceParams.flags = 0;
		surfaceParams.buffer_types[0] = SBT_RGBA;

		if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
			surfaceParams.buffer_types[0] |= SBT_SRGB;

		rdrSetDefaultTexFlagsForSurfaceParams( &surfaceParams );

		pAtlas->pRdrSurface = rdrCreateSurface(gfx_state.currentDevice->rdr_device, &surfaceParams);
		pAtlas->pTexture = texAllocateScratch(NULL, surfaceParams.width, surfaceParams.height, WL_FOR_ENTITY);
		pAtlas->pTexture->name = allocAddString(surfaceParams.name);
		texAllocRareData(pAtlas->pTexture)->dont_free_handle = true;
		pAtlas->pTexture->tex_handle = rdrSurfaceToTexHandle(pAtlas->pRdrSurface, 0);
	}

	++gfxImposterDebug.uiNumImposterAtlas;
	if (gfxImposterDebug.uiNumImposterAtlas > 10)
	{
		ErrorDetailsf("%d Imposter Atlases allocated.", gfxImposterDebug.uiNumImposterAtlas);
		Errorf("Allocated too many Imposter Atlases");
	}

	MAX1(gfxImposterDebug.uiPeakNumImposterAtlas, gfxImposterDebug.uiNumImposterAtlas);

	return pAtlas;
}

S32 gfxImposterAtlasGetFreeSectionIndex(GfxImposterAtlas* pAtlas)
{
	S32 iIndex;
	for (iIndex=0; iIndex<pAtlas->specs.iNumSections; ++iIndex)
	{
		if (pAtlas->pSections[iIndex].iRefCount <= 0)
		{
			pAtlas->pSections[iIndex].iRefCount = 1;
			++pAtlas->iUsedSections;
			return iIndex;
		}
	}
	return -1;
}



void gfxImposterAtlasReleaseSection(GfxImposterAtlas* pAtlas, S32 iSectionIndex)
{
	assert(iSectionIndex >= 0 && iSectionIndex < pAtlas->specs.iNumSections);
	assert(pAtlas->pSections[iSectionIndex].iRefCount > 0);
	--pAtlas->pSections[iSectionIndex].iRefCount;
	--gfxImposterDebug.uiNumImposters;
	if (--pAtlas->iUsedSections == 0 && pAtlas->iAtlasIndex >= NUM_ATLASES_KEEP_AROUND)
	{
		memlog_printf(NULL, "Atlas empty %p %d S %p BT %p %p CF[%d]\n", pAtlas, pAtlas->iAtlasIndex, pAtlas->pRdrSurface, pAtlas->pTexture, pAtlas->pTexture->actualTexture, gfx_state.frame_count);
		pAtlas->iNumFramesEmpty = 1;
	}
}

static void gfxAtlasSetViewport(GfxImposterAtlas* pAtlas, S32 iSectionIndex, Vec4 vViewport)
{
	vViewport[0] = 1.0f / pAtlas->iCols;
	vViewport[1] = 1.0f / pAtlas->iRows;
	vViewport[2] = (F32)(iSectionIndex % pAtlas->iCols) / (F32)(pAtlas->iCols);
	vViewport[3] = (F32)(iSectionIndex / pAtlas->iCols) / (F32)(pAtlas->iRows);
}













// Bodysock specific stuff
static GfxImposterAtlas** eaBodysockAtlas = NULL;

// Must all be powers of 2!
const GfxImposterAtlasSpecs bodysockAtlasSpecs = 
{
	64, // bodysock tex width
	64, // bodysock tex height
	1024, // max surface width
	32, // num tex per surface
};

typedef struct GfxBodysockTextureDrawData
{
	DynNode* pRoot;
	DynSkeleton* pSkeleton;
	DynDrawSkeleton* pDrawSkel;
	Vec3 vExtentsMin, vExtentsMax;
	GfxImposterAtlas* pAtlas;
	S32 iSectionIndex;
	GfxCameraView* pCamView;
	GfxCameraController* pCamController;
	const char* pcPose;
	U32 uiFrameCount;
} GfxBodysockTextureDrawData;

static GfxBodysockTextureDrawData** eaBodysocksToRender = NULL;
static GfxBodysockTextureDrawData** eaBodysocksToDestroy = NULL;
static RdrSurface** eaSurfacesToDestroy = NULL;

static void gfxImposterAtlasFree(GfxImposterAtlas* pAtlas)
{
	memlog_printf(NULL, "Freeing atlas %p %d S %p BT %p %p CF[%d]\n", pAtlas, pAtlas->iAtlasIndex, pAtlas->pRdrSurface, pAtlas->pTexture, pAtlas->pTexture->actualTexture, gfx_state.frame_count);
	// remove it from list
	eaRemoveFast(&eaBodysockAtlas, pAtlas->iAtlasIndex);

	// Fixup indices
	if (eaSize(&eaBodysockAtlas) > pAtlas->iAtlasIndex)
		eaBodysockAtlas[pAtlas->iAtlasIndex]->iAtlasIndex = pAtlas->iAtlasIndex;

	// free it
	eaPush(&eaSurfacesToDestroy, pAtlas->pRdrSurface);
	texGenFreeNextFrame(pAtlas->pTexture);
	free(pAtlas->pSections);
	free(pAtlas);

	--gfxImposterDebug.uiNumImposterAtlas;
}

bool gfxImposterAtlasRenderCostume(GfxBodysockTextureDrawData* pDrawData)
{
	bool bLoaded;
	// Make sure the draw skeleton is ready to draw
	// Unfortunately, we're doing this in a hacky sort of way right now
	bLoaded = (pDrawData->uiFrameCount >= 3 && !gfxIsStillLoading(true));

	if (!gfxIsActionFull(GfxAction_ImpostorAtlas))
	{
		GfxTempSurface* pTempSurface;
		WorldRegion* pRegion;
		Mat4 mCam;
		Vec3 vCamDir = { 0.0f, 0.0f, 1.0f };
		Vec3 vCamPos = { 0.0f, 2.0f, 1.0f };
		F32 fAspectRatio = 1.0f;
		F32 fFrustumW, fFrustumH;
		F32 fOrthoZoom;

		WorldGraphicsData *pWorldGraphicsData;
		static GfxLight *pImposterSunlight = NULL;
		static GfxLight *pImposterSunlight2 = NULL;
		GfxLight *pRealSunlight = NULL;
		GfxLight *pRealSunlight2 = NULL;

		++pDrawData->uiFrameCount;

		// Render the bodysock texture onto the new atlas section
		gfxBeginSection( "BodysockAtlas" );
		gfxAuxDeviceSaveParams( true );

		pRegion =  worldGetTempWorldRegionByName("BodysockAtlas");

		dynNodeSetPos(pDrawData->pRoot, zerovec3);
		dynNodeSetRot(pDrawData->pRoot, unitquat);
		dynNodeCleanDirtyBits(pDrawData->pRoot);
		if (pDrawData->pcPose)
		{
			if (!dynSkeletonForceAnimation(pDrawData->pSkeleton, pDrawData->pcPose, 0.0f))
			{
				bLoaded = false;
				pDrawData->pSkeleton->pRoot->uiDirtyBits = 1;
				dynDrawSkeletonBasePose(pDrawData->pDrawSkel);
			}
		}
		else
		{
			pDrawData->pSkeleton->pRoot->uiDirtyBits = 1;
			dynDrawSkeletonBasePose(pDrawData->pDrawSkel);
		}

		//dynSkeletonGetExpensiveExtents(pDrawData->pSkeleton, vSkelMin, vSkelMax, 0.15f);

		fFrustumW = 2.0f * (pDrawData->vExtentsMax[0] - pDrawData->vExtentsMin[0]);
		fFrustumH = pDrawData->vExtentsMax[1] - pDrawData->vExtentsMin[1];
		fOrthoZoom = fFrustumH * 0.5f;
		fAspectRatio = fFrustumW / fFrustumH;

		// Set the camera to be centered on the two skeletons. distance doesn't matter due to orthographic projection
		setVec3(vCamPos, pDrawData->vExtentsMax[0], fFrustumH * 0.5f, 1.0f );

		// We need to figure out camera bounds
		// Should be basically the camera extents, but doubled up in the X



		camLookAt( vCamDir, mCam );
		copyVec3( vCamPos, mCam[3] );
		frustumSetCameraMatrix( &pDrawData->pCamView->frustum, mCam );

		gfxSetActiveCameraView( pDrawData->pCamView, false );
		gfxSetActiveCameraController( pDrawData->pCamController, false );

		gfxCameraControllerSetSkyOverride( pDrawData->pCamController, IMPOSTER_SKY_NAME, NULL );

		{
			Vec4 vClearColor = { 0.5f, 0.5f, 0.5f, 1.0f };
			gfxActiveCameraControllerOverrideClearColor(vClearColor);
		}

		pDrawData->pCamController->ortho_mode = true;
		pDrawData->pCamController->ortho_zoom = fOrthoZoom;
		gfxRunActiveCameraController(fAspectRatio, NULL);

		pDrawData->pCamView->adapted_light_range_inited = 0;
		pDrawData->pCamView->desired_light_range = 1.0f;

		{
			RdrSurfaceParams surfaceParams = {0};

			surfaceParams.name = "GfxImposterAtlas";
			rdrSurfaceParamSetSizeSafe(&surfaceParams, pDrawData->pAtlas->specs.iSectionResW,
				pDrawData->pAtlas->specs.iSectionResH);
			surfaceParams.desired_multisample_level = 1;
			surfaceParams.required_multisample_level = 1;
			surfaceParams.depth_bits = 24;
			surfaceParams.flags = 0;
			surfaceParams.buffer_types[0] = SBT_RGBA;

			if (gfxFeatureEnabled(GFEATURE_LINEARLIGHTING))
				surfaceParams.buffer_types[0] |= SBT_SRGB;

			if (gfx_state.debug.bodysockTexAA)
			{
				surfaceParams.width *= 4;
				surfaceParams.height *= 4;
			}

			rdrSetDefaultTexFlagsForSurfaceParams( &surfaceParams );

			pTempSurface = gfxGetTempSurface(&surfaceParams);
		}


		gfxSkyClearVisibleSky( pDrawData->pCamView->sky_data );
		
		// FIXME: Workaround for the global WorldGraphicsData that sun
		//   light information is stored in. Preserve the "real" sun
		//   light.
		pWorldGraphicsData = worldGetWorldGraphicsData();
		pRealSunlight = pWorldGraphicsData->sun_light;
		pRealSunlight2 = pWorldGraphicsData->sun_light_2;
		pWorldGraphicsData->sun_light = pImposterSunlight;
		pWorldGraphicsData->sun_light_2 = pImposterSunlight2;

		if (gfxStartAction(pTempSurface->surface, 0, pRegion, pDrawData->pCamView, pDrawData->pCamController, false, false, true, false, false, false, false, false, false, GfxAction_ImpostorAtlas, false, 0))
		{
			gfx_state.currentAction->postRenderBlitOutputSurface = pDrawData->pAtlas->pRdrSurface;
			gfxAtlasSetViewport(pDrawData->pAtlas, pDrawData->iSectionIndex, gfx_state.currentAction->postRenderBlitViewport);
#if PLATFORM_CONSOLE
			gfx_state.currentAction->postRenderBlitDestPixel[0] = (pDrawData->iSectionIndex % pDrawData->pAtlas->iCols) * pDrawData->pAtlas->specs.iSectionResW;
			gfx_state.currentAction->postRenderBlitDestPixel[1] = (pDrawData->iSectionIndex / pDrawData->pAtlas->iCols) * pDrawData->pAtlas->specs.iSectionResH;
#endif

			// Draw Front
			gfxQueueSingleDynDrawSkeleton(pDrawData->pDrawSkel, pRegion, false, false, NULL);

			// Back
			{
				Vec3 vPos = { -pDrawData->vExtentsMin[0] * 2.0f, 0.0f, 0.0f };
				Quat qRot;
				axisAngleToQuat(upvec, PI, qRot);
				dynNodeSetPos(pDrawData->pRoot, vPos);
				dynNodeSetRot(pDrawData->pRoot, qRot);
				dynNodeCleanDirtyBits(pDrawData->pRoot);

				if (pDrawData->pcPose)
				{
					if (!dynSkeletonForceAnimation(pDrawData->pSkeleton, pDrawData->pcPose, 0.0f))
					{
						bLoaded = false;
						pDrawData->pSkeleton->pRoot->uiDirtyBits = 1;
						dynDrawSkeletonBasePose(pDrawData->pDrawSkel);
					}
				}
				else
				{
					pDrawData->pSkeleton->pRoot->uiDirtyBits = 1;
					dynDrawSkeletonBasePose(pDrawData->pDrawSkel);
				}
				gfxQueueSingleDynDrawSkeleton(pDrawData->pDrawSkel, pRegion, false, false, NULL);
			}

			gfxFillDrawList( false , NULL);

			// freed in gfxActionReleaseSurfaces() after the aciton finishes, doing so here is bad!
			//gfxReleaseTempSurface(pTempSurface);
			assert(!gfx_state.currentAction->freeMe);
			gfx_state.currentAction->freeMe = pTempSurface;

			gfxFinishAction();
		}
		else
		{
			bLoaded = false;
			gfxReleaseTempSurface(pTempSurface);
		}

		// FIXME: Global WorldGraphicsData workaround. Restore the
		//   "real" sun light and save the imposter sun light.
		pImposterSunlight = pWorldGraphicsData->sun_light;
		pImposterSunlight2 = pWorldGraphicsData->sun_light_2;
		pWorldGraphicsData->sun_light = pRealSunlight;
		pWorldGraphicsData->sun_light_2 = pRealSunlight2;

		gfxAuxDeviceSaveParams( false );
		gfxEndSection();

	}
	else
	{
		bLoaded = false;
	}

	return bLoaded;
}

GfxBodysockTextureDrawData* gfxBodysockTextureDrawDataCreate(WLCostume* pCostume, GfxImposterAtlas* pAtlas, S32 iSectionIndex)
{
	GfxBodysockTextureDrawData* pDrawData = calloc(sizeof(GfxBodysockTextureDrawData), 1);

	pDrawData->pSkeleton = dynSkeletonCreate(pCostume, false, true, false, true, false, NULL);
	pDrawData->pDrawSkel = dynDrawSkeletonCreate(pDrawData->pSkeleton, pCostume, NULL, false, false, true);
	pDrawData->pRoot = dynNodeAlloc();
	dynNodeParent(pDrawData->pSkeleton->pRoot, pDrawData->pRoot);

	{
		if (pCostume->pBodySockInfo)
		{
			copyVec3(pCostume->pBodySockInfo->vBodySockMin, pDrawData->vExtentsMin);
			copyVec3(pCostume->pBodySockInfo->vBodySockMax, pDrawData->vExtentsMax);
		}
	}

	pDrawData->pSkeleton->bVisibilityChecked = pDrawData->pSkeleton->bVisible = true;
	pDrawData->pSkeleton->uiLODLevel = worldLibGetLODSettings()->uiBodySockLODLevel;
	pDrawData->pDrawSkel->bForceUnlit = true;
	pDrawData->pcPose = pCostume->pcBodysockPose;
	if (pDrawData->pcPose)
	{
		dynSkeletonForceAnimationPrepare(pDrawData->pcPose);
	}

	pDrawData->pAtlas = pAtlas;
	pDrawData->iSectionIndex = iSectionIndex;
	pDrawData->pCamView = calloc(sizeof(GfxCameraView), 1);
	pDrawData->pCamController = calloc(sizeof(GfxCameraController), 1);
	setVec4( pDrawData->pCamView->exposure_transform, 1 / 1.0, 1.0, 0, 0 );
	gfxInitCameraView( pDrawData->pCamView );
	gfxInitCameraController( pDrawData->pCamController, gfxNullCamFunc, NULL );

	return pDrawData;
}

void gfxBodysockTextureDrawDataDestroy( GfxBodysockTextureDrawData* pDrawData )
{
	dynDrawSkeletonFree(pDrawData->pDrawSkel);
	dynSkeletonFree(pDrawData->pSkeleton);
	dynNodeFree(pDrawData->pRoot);
	gfxDeinitCameraView(pDrawData->pCamView);
	free(pDrawData->pCamView);
	gfxDeinitCameraController(pDrawData->pCamController);
	free(pDrawData->pCamController);
}


BasicTexture* gfxImposterAtlasGetBodysockTexture(WLCostume* pCostume, S32* piSectionIndex, Vec4 vTexXFrm)
{
	// First, try to find a free section in the current atlases
	GfxImposterAtlas* pAtlas = NULL;
	S32 iSectionIndex;
	FOR_EACH_IN_EARRAY_FORWARDS(eaBodysockAtlas, GfxImposterAtlas, pPossibleAtlas)
		if (pPossibleAtlas->iUsedSections >= pPossibleAtlas->specs.iNumSections)
			continue;
		iSectionIndex = gfxImposterAtlasGetFreeSectionIndex(pPossibleAtlas);
		if (iSectionIndex >= 0)
		{
			pAtlas = pPossibleAtlas;
			break;
		}
	FOR_EACH_END;

	// If none can be found, create a new atlas and use a section from that
	if (!pAtlas)
	{
		pAtlas = gfxImposterAtlasCreate(&bodysockAtlasSpecs, eaSize(&eaBodysockAtlas));
		iSectionIndex = gfxImposterAtlasGetFreeSectionIndex(pAtlas);
		assert(iSectionIndex >= 0);
		eaPush(&eaBodysockAtlas, pAtlas);
	}

	// Create the data that we want to draw
	// This has to wait for all of the models to load
	// so we can't do the actual rendering right away
	eaPush(&eaBodysocksToRender, gfxBodysockTextureDrawDataCreate(pCostume, pAtlas, iSectionIndex) );

	++gfxImposterDebug.uiNumImposters;

	// Return the global bodysock section index and tex handle
	*piSectionIndex = iSectionIndex;
	gfxAtlasSetViewport(pAtlas, iSectionIndex, vTexXFrm);
	return pAtlas->pTexture;
}

bool gfxImposterAtlasReleaseBodysockTexture(BasicTexture* pTexture, S32 iSectionIndex)
{
	FOR_EACH_IN_EARRAY(eaBodysockAtlas, GfxImposterAtlas, pAtlas)
		if (pAtlas->pTexture == pTexture)
		{
			gfxImposterAtlasReleaseSection(pAtlas, iSectionIndex);
			FOR_EACH_IN_EARRAY(eaBodysocksToRender, GfxBodysockTextureDrawData, pDrawData)
				if (pDrawData->pAtlas == pAtlas && pDrawData->iSectionIndex == iSectionIndex)
				{
					eaPush(&eaBodysocksToDestroy, pDrawData);
					eaRemoveFast(&eaBodysocksToRender, ipDrawDataIndex);
				}
			FOR_EACH_END;
			return true;
		}
	FOR_EACH_END;
	return false;
}

void gfxImposterAtlasDrawBodysockAtlas(void)
{
	FOR_EACH_IN_EARRAY(eaBodysockAtlas, GfxImposterAtlas, pAtlas)
		int i;
		bool bShouldDraw = false;
		for (i=0; i<pAtlas->specs.iNumSections; ++i)
		{
			if (pAtlas->pSections[i].iRefCount > 0)
			{
				bShouldDraw = true;
				break;
			}
		}
		if (bShouldDraw)
			gfxDebugThumbnailsAddSurface(pAtlas->pRdrSurface, 0, 0, "GfxImposterAtlas", 0);
	FOR_EACH_END;
}

bool gfxImposterAtlasDumpBodysockAtlasToFile(const char* pcFileName)
{
	if (eaSize(&eaBodysockAtlas) > 0)
	{
		GfxImposterAtlas* pAtlas = eaBodysockAtlas[0];
		U8* data;
		char destFile[ MAX_PATH ];
		U32 iW = pAtlas->specs.iSectionResW;
		U32 iH = pAtlas->specs.iSectionResH;
		U32 x, y, line_size = iW * 4;
		U8 *data_flipped = malloc(iH * line_size);

		gfxSetActiveSurface(pAtlas->pRdrSurface);
		data = rdrGetActiveSurfaceData(gfx_state.currentDevice->rdr_device, SURFDATA_RGBA, iW, iH);

		for (y = 0; y < iH; ++y)
		{
			U32 idx1 = y * line_size;
			U32 idx2 = (iH - y - 1) * line_size;
			for (x = 0; x < iW; ++x)
			{
				data_flipped[idx2+0] = data[idx1+0];
				data_flipped[idx2+1] = data[idx1+1];
				data_flipped[idx2+2] = data[idx1+2];
				data_flipped[idx2+3] = 255;
				idx1+=4;
				idx2+=4;
			}
		}

		fileSpecialDir( "screenshots", SAFESTR( destFile ));
		strcat( destFile, "/" );
		strcat( destFile, pcFileName );
		tgaSave( destFile, data_flipped, iW, iH, 4);
		free( data );
		free(data_flipped);
	}
	return true;
}

AUTO_COMMAND ACMD_NAME(danimSaveBodysockAtlas) ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(dynAnimation);
void gfxSaveBodysockAtlas(char *filename)
{
	if (!filename)
		return;
	changeFileExt(filename, ".tga", gfx_state.bodysockatlas_filename);
	gfx_state.bodysockatlas_screenshot = true;
}

void gfxImposterFreeSurface(RdrSurface* pSurface)
{
	gfxSurfaceDestroy(pSurface);
}

void gfxImposterAtlasOncePerFrame(void)
{
	static U32 uiLastCallFrameCount = 0xFFFFFFFF;
	bool bDidSomething=false;

	// Make sure this function is never called twice per frame, as it will free data before it's ready
	if (gfx_state.frame_count == uiLastCallFrameCount)
		return;

	PERFINFO_AUTO_START_FUNC();

	uiLastCallFrameCount = gfx_state.frame_count;

	// Call this one before we destory the bodysock textures, so that it will only free the surface next frame
	eaClearEx(&eaSurfacesToDestroy, gfxImposterFreeSurface);
	
	eaClearEx(&eaBodysocksToDestroy, gfxBodysockTextureDrawDataDestroy);

	FOR_EACH_IN_EARRAY_FORWARDS(eaBodysocksToRender, GfxBodysockTextureDrawData, pDrawData)
	{
		bDidSomething = true;
		if (
			(pDrawData->pSkeleton && RefSystem_GetReferenceCountForReferent(GET_REF(pDrawData->pSkeleton->hCostume)) <= 1) // Not used by anyone but us, so free it
			|| gfxImposterAtlasRenderCostume(pDrawData) // done rendering, so free it
			)
		{
			eaPush(&eaBodysocksToDestroy, pDrawData);
			eaRemove(&eaBodysocksToRender, ipDrawDataIndex);
			--ipDrawDataIndex;
			--cipDrawDataNum;
		}
	}
	FOR_EACH_END;

	FOR_EACH_IN_EARRAY(eaBodysockAtlas, GfxImposterAtlas, pAtlas)
	{
		assert(ipAtlasIndex == pAtlas->iAtlasIndex);
		if (pAtlas->iUsedSections > 0 || ipAtlasIndex < NUM_ATLASES_KEEP_AROUND)
		{
			pAtlas->iNumFramesEmpty = 0;
			continue;
		}

		if (pAtlas->iNumFramesEmpty > 0)
		{
			memlog_printf(NULL, "Atlas %p %d S %p BT %p %p num f empty %d CF[%d]\n", pAtlas, pAtlas->iAtlasIndex, pAtlas->pRdrSurface, pAtlas->pTexture, pAtlas->pTexture->actualTexture, pAtlas->iNumFramesEmpty, gfx_state.frame_count);
			if (pAtlas->iNumFramesEmpty++ > NUM_EMPTY_FRAMES_BEFORE_DELETION)
			{
				// deletion time
				gfxImposterAtlasFree(pAtlas);
				continue;
			}
		}
	}
	FOR_EACH_END;

	if (bDidSomething)
	{
		worldCellEntryClearTempGeoDraws();
		gfxDynDrawModelClearTempMaterials(true);
		dynSortBucketClearCache();
	}

	PERFINFO_AUTO_STOP();
}
