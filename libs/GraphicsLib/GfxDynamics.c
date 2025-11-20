#include "Queue.h"
#include "rgb_hsv.h"
#include "ScratchStack.h"
#include "rand.h"
#include "tex_gen.h"
#include "EventTimingLog.h"
#include "MemRef.h"
#include "bounds.h"

#include "RdrState.h"

#include "wlModelInline.h"
#include "wlCostume.h"
#include "WorldLib.h"
#include "wlAutoLOD.h"

#include "dynFxParticle.h"
#include "dynFxInfo.h"
#include "dynFxDebug.h"
#include "dynFxFastParticle.h"
#include "dynNodeInline.h"
#include "dynCloth.h"
#include "dynClothMesh.h"
#include "dynRagdollData.h"

#include "GraphicsLib.h"
#include "GfxDynamics.h"
#include "GfxTexturesInline.h"
#include "GfxMaterials.h"
#include "GfxLights.h"
#include "GfxLightCache.h"
#include "GfxLightsPrivate.h"
#include "GfxPrimitive.h"
#include "GfxWorld.h"
#include "GfxOcclusion.h"
#include "GfxTextureTools.h"
#include "GfxSplat.h"
#include "GfxShadows.h"
#include "GfxDrawFrame.h"
#include "GfxGeo.h"
#include "GfxSky.h"
#include "GfxTexWords.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Renderer););

#define FAST_PARTICLE_CUTOUT_DEPTH 5.0f
#define UV_DENSITY_FOR_FAST_PARTICLES TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY  // Need different/better uv_density here?  Not sure we can calc it
#define UV_DENSITY_FOR_CLOTH TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY // Hardcoding approximate texel density - we could figure this out if giant monsters have capes

typedef struct DynDrawParams
{
	WLDynDrawParams		wl_params; // MUST BE FIRST
	int					frustum_visible;

	U32 				bIgnoreNode:1;
	U32 				noMaterialPreSwap:1;
	U32 				bIsInFPEditor:1;
	U32					bForceSkinnedShadows:1;
	U32					queued_as_alpha:1;
	
	DynSplat*			pSplatInfo;

	Vec3 				lights_root;
	F32 				lights_radius;
	F32 				fScreenArea;

	Mat4				root_mat;

	WorldRegionGraphicsData *region_graphics_data;

	RdrMaterialFlags	additional_material_flags;

} DynDrawParams;

static DynFxFastParticleSet** eaEditorFastParticleSets = NULL;
static DynFxFastParticleSet** eaEditorFastParticleSets2d = NULL;

static WorldDrawableListPool fx_drawable_pool;

//////////////////////////////////////////////////////////////////////////
// Queueing functions

static MaterialNamedConstant **spriteNamedConstants = NULL;
static MaterialNamedConstant **editableSpriteNamedConstants = NULL;


AUTO_RUN;
void gfxInitSpriteMaterialParams(void)
{
	int i;
	for (i=0; i<NUM_SPRITE_NAMED_CONSTANTS; ++i)
	{
		MaterialNamedConstant* pConstant = calloc(sizeof(MaterialNamedConstant), 1);
		pConstant->name = allocAddString(pcSpriteNamedConstants[i]);
		eaPush(&spriteNamedConstants, pConstant);
	}
	for (i=0; i<NUM_SPRITE_NAMED_CONSTANTS; ++i)
	{
		MaterialNamedConstant* pConstant = calloc(sizeof(MaterialNamedConstant), 1);
		pConstant->name = allocAddString(pcSpriteNamedConstants[i]);
		eaPush(&editableSpriteNamedConstants, pConstant);
	}
}

static MaterialNamedConstant **geometryNamedConstants = NULL;
static MaterialNamedConstant **editableGeometryNamedConstants = NULL;

AUTO_RUN;
void gfxInitGeometryMaterialParamStrings(void)
{
	int i;
	for (i=2; i<NUM_SPRITE_NAMED_CONSTANTS; ++i)
	{
		MaterialNamedConstant* pConstant = calloc(sizeof(MaterialNamedConstant), 1);
		pConstant->name = allocAddString(pcSpriteNamedConstants[i]);
		eaPush(&geometryNamedConstants, pConstant);
	}
	for (i=2; i<NUM_SPRITE_NAMED_CONSTANTS; ++i)
	{
		MaterialNamedConstant* pConstant = calloc(sizeof(MaterialNamedConstant), 1);
		pConstant->name = allocAddString(pcSpriteNamedConstants[i]);
		eaPush(&editableGeometryNamedConstants, pConstant);
	}
}

static void gfxResetEditableSpriteMNCs(void)
{
	int i;
	for (i=2; i<NUM_SPRITE_NAMED_CONSTANTS; ++i)
	{
		editableSpriteNamedConstants[i]->name = allocAddString(pcSpriteNamedConstants[i]);
	}
}

static void gfxResetEditableGeometryMNCs(void)
{
	int i;
	for (i=2; i<NUM_SPRITE_NAMED_CONSTANTS; ++i)
	{
		editableGeometryNamedConstants[i-2]->name = allocAddString(pcSpriteNamedConstants[i]);
	}
}


__forceinline static RdrMaterialFlags dynNodeGetRdrBlendFlags(DynBlendMode iBlendMode)
{
	RdrMaterialFlags blendFlags = 0;
	if ( iBlendMode == DynBlendMode_Additive )
		blendFlags = RMATERIAL_ADDITIVE;
	else
	if ( iBlendMode == DynBlendMode_Subtractive )
		blendFlags = RMATERIAL_SUBTRACTIVE;
	return blendFlags;
}

void gfxPrepareDynParticleMNCs( DynMNCRename*** peaMNCRename, MaterialNamedConstant*** peaMNC, bool bMultiColor, DynDrawParticle* pDraw ) 
{
	if (bMultiColor)
	{
		if (peaMNCRename)
		{
			// Replace geometryNamedConstants with our own version
			gfxResetEditableSpriteMNCs();
			FOR_EACH_IN_EARRAY((*peaMNCRename), DynMNCRename, pMNCRename)
			{
				FOR_EACH_IN_EARRAY(editableSpriteNamedConstants, MaterialNamedConstant, pMNC)
				{

					if (pMNCRename->pcBefore == pMNC->name)
						pMNC->name = pMNCRename->pcAfter;
				}
				FOR_EACH_END;
			}
			FOR_EACH_END;
			*peaMNC = editableSpriteNamedConstants;
		}
		assert(*peaMNC);

		// scale by 1 / 255
		if (pDraw->fHueShift || pDraw->fSaturationShift || pDraw->fValueShift)
		{
			hsvShiftRGB(pDraw->vColor1, (*peaMNC)[2]->value, pDraw->fHueShift, pDraw->fSaturationShift, pDraw->fValueShift);
			hsvShiftRGB(pDraw->vColor2, (*peaMNC)[3]->value, pDraw->fHueShift, pDraw->fSaturationShift, pDraw->fValueShift);
			hsvShiftRGB(pDraw->vColor3, (*peaMNC)[4]->value, pDraw->fHueShift, pDraw->fSaturationShift, pDraw->fValueShift);

			scaleVec4((*peaMNC)[2]->value, U8TOF32_COLOR, (*peaMNC)[2]->value);
			scaleVec4((*peaMNC)[3]->value, U8TOF32_COLOR, (*peaMNC)[3]->value);
			scaleVec4((*peaMNC)[4]->value, U8TOF32_COLOR, (*peaMNC)[4]->value);
		}
		else
		{
			scaleVec4(pDraw->vColor1, U8TOF32_COLOR, (*peaMNC)[2]->value);
			scaleVec4(pDraw->vColor2, U8TOF32_COLOR, (*peaMNC)[3]->value);
			scaleVec4(pDraw->vColor3, U8TOF32_COLOR, (*peaMNC)[4]->value);
		}
	}
	else
	{
		Vec4 vShiftedColor;

		hsvShiftRGB(
			pDraw->vColor,
			vShiftedColor,
			pDraw->vHSVShift[0], 
			pDraw->vHSVShift[1],
			pDraw->vHSVShift[2]);

		vShiftedColor[3] = pDraw->vColor[3];

		scaleVec4(vShiftedColor, U8TOF32_COLOR, (*peaMNC)[2]->value);
		scaleVec4(vShiftedColor, U8TOF32_COLOR, (*peaMNC)[3]->value);
		scaleVec4(vShiftedColor, U8TOF32_COLOR, (*peaMNC)[4]->value);
	}

	setVec4((*peaMNC)[0]->value, pDraw->vTexScale[0]?pDraw->vTexScale[0]:1.0f, pDraw->vTexScale[1]?pDraw->vTexScale[1]:1.0f, pDraw->vTexOffset[0], pDraw->vTexOffset[1]);
	setVec4((*peaMNC)[1]->value, pDraw->vTexScale[0]?pDraw->vTexScale[0]:1.0f, pDraw->vTexScale[1]?pDraw->vTexScale[1]:1.0f, pDraw->vTexOffset[0], pDraw->vTexOffset[1]);
}

static void gfxQueueSpriteParticle(DynParticle *pParticle, const DynDrawParams *draw_params)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	DynDrawParticle* pDraw = pParticle->pDraw;
	F32 alpha = pDraw->vColor[3] * pParticle->fFadeOut * U8TOF32_COLOR;
	RdrDrawableParticle *pPartDraw = NULL;
	RdrDrawableParticleLinkList *pPartLink;
	RdrAddInstanceParams instance_params={0};
	RdrInstancePerDrawableData per_drawable_data;
	RdrDrawListPassData *pass = gdraw->visual_pass;
	Vec3 world_mid, vScale;
	F32 fUvDensity, zdist;
	MaterialNamedConstant ***peaMNC = &spriteNamedConstants;
	DynFxSortBucket *pSortBucket = draw_params->wl_params.sort_bucket;
	RdrMaterialFlags blend_flags;
	bool bCreatedDrawable = false;
	RdrDrawList *draw_list = gdraw->draw_list;

	if (!pass || !(draw_params->frustum_visible & pass->frustum_set_bit))
		return;

	if (draw_params->wl_params.is_screen_space)
	{
		draw_list = gfx_state.currentDevice->draw_list2d;

		// HACK - Bail out of drawing screen space
		// particles if the 2D draw list isn't set up yet.
		// -Cliff
		if(!rdrDrawListHasData(draw_list)) return;
	}

	blend_flags = dynNodeGetRdrBlendFlags(pDraw->iBlendMode);

	instance_params.wireframe = dynDebugState.bFxNotWireframe?0:gdraw->global_wireframe;
	setVec3same(instance_params.ambient_multiplier, 1);

	instance_params.uses_far_depth_range = gdraw->use_far_depth_range;
	if (pParticle->bLowRes)
		instance_params.add_material_flags |= RMATERIAL_LOW_RES_ALPHA;

	dynNodeGetWorldSpacePos(&pDraw->node, world_mid);
	addVec3(world_mid, gdraw->pos_offset, world_mid);

	dynNodeGetWorldSpaceScale(&pDraw->node, vScale);
	fUvDensity = MAXF(fabsf(vScale[0]), MAXF(fabsf(vScale[1]), fabsf(vScale[2])));
	if (fUvDensity)
		fUvDensity = -log2f(fUvDensity);
	else
		fUvDensity = TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY;

	gfxPrepareDynParticleMNCs(pParticle->peaMNCRename, peaMNC, pParticle->bMultiColor, pDraw);

	instance_params.per_drawable_data = &per_drawable_data;

	zdist = rdrDrawListCalcZDist(draw_list, world_mid) - pDraw->fTightenUp;
	MAX1F(zdist, 0.0f);

	if (pSortBucket)
	{
		int i;
		for (i = 0; i < eaSize(&pSortBucket->eaCachedDrawables); ++i)
		{
			RdrDrawableParticle *pCachedPartDraw = pSortBucket->eaCachedDrawables[i];

			// CD: note that this doesn't check the material... it is difficult to do so at any kind of good speed
			// so I'm leaving it out.  The artists will just have to be careful what they put in the same sort bucket.
			if (pCachedPartDraw->is_screen_space == draw_params->wl_params.is_screen_space &&
				pCachedPartDraw->blend_flags == blend_flags &&
				pCachedPartDraw->tex_handle0 == pDraw->hTexHandle &&
				pCachedPartDraw->tex_handle1 == pDraw->hTexHandle2)
			{
				pPartDraw = pCachedPartDraw;
				rdrDrawListAddParticleLink(draw_list, pPartDraw);
				++dynDebugState.frameCounters.uiNumBatchedSlowParticles;
				if (dynDebugState.bFxDebugOn)
				{
					DynFxInfo* pFxInfo = draw_params->wl_params.pInfo;
					DynDrawTracker* pDrawTracker = dynDrawTrackerFind(pFxInfo->pcDynName);
					if (pDrawTracker)
						++pDrawTracker->uiSubObjects;
				}
				break;
			}
		}
	}

	if (!pPartDraw)
	{
		pPartDraw = rdrDrawListAllocParticle(draw_list, RTYPE_PARTICLE);
		if (!pPartDraw)
			return;

		if (draw_params->wl_params.is_costume)
			++dynDebugState.frameCounters.uiNumDrawnCostumeFx;
		else if (draw_params->wl_params.is_debris)
			++dynDebugState.frameCounters.uiNumDrawnDebris;
		else
		{
			++dynDebugState.frameCounters.uiNumDrawnFx;
			if (dynDebugState.bFxDebugOn)
			{
				DynDrawTracker* pDrawTracker = dynDrawTrackerPush();
				DynFxInfo* pFxInfo = draw_params->wl_params.pInfo;
				pDrawTracker->pcName = pFxInfo->pcDynName;
				pDrawTracker->ePriority = draw_params->wl_params.iPriorityLevel;
				pDrawTracker->eType = eDynFxType_Sprite;
				pDrawTracker->uiSubObjects = 1;
				pDrawTracker->uiNum = 1;
				if (dynDebugState.bFxDrawOnlySelected)
				{
					bool bDraw = false;
					FOR_EACH_IN_EARRAY(eaDynDrawOnly, const char, pcDrawOnly)
					{
						if (pcDrawOnly == pFxInfo->pcDynName)
						{
							bDraw = true;
							break;
						}
					}
					FOR_EACH_END;

					if (!bDraw)
						return;
				}
			}
		}
		++dynDebugState.frameCounters.uiNumDrawnSlowParticles;

		pPartDraw->is_screen_space = draw_params->wl_params.is_screen_space;
		pPartDraw->blend_flags = blend_flags;
		pPartDraw->tex_handle0 = pDraw->hTexHandle;
		pPartDraw->tex_handle1 = pDraw->hTexHandle2;

		RDRALLOC_SUBOBJECT_PTRS(instance_params, 1);
		instance_params.subobjects[0] = rdrDrawListAllocSubobject(draw_list, 2);

		gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[0], pDraw->pMaterial, NULL, peaMNC?(*peaMNC):NULL, NULL, NULL, per_drawable_data.instance_param,
			draw_params->wl_params.is_screen_space?0.0f:zdist, draw_params->wl_params.is_screen_space?TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY:fUvDensity);

		bCreatedDrawable = true;

		if (pSortBucket)
			eaPush(&pSortBucket->eaCachedDrawables, pPartDraw);
	}

	pPartLink = pPartDraw->particles;
	pPartLink->zdist = zdist;


	// This is kind of ugly, but works for now
	if ( pDraw->bHInvert && pDraw->bVInvert )
	{
		setVec2(pPartLink->verts[3].texcoord, 0, 0);
		setVec2(pPartLink->verts[2].texcoord, 0, 1);
		setVec2(pPartLink->verts[1].texcoord, 1, 1);
		setVec2(pPartLink->verts[0].texcoord, 1, 0);
	}
	else if ( pDraw->bHInvert )
	{
		setVec2(pPartLink->verts[2].texcoord, 0, 0);
		setVec2(pPartLink->verts[3].texcoord, 0, 1);
		setVec2(pPartLink->verts[0].texcoord, 1, 1);
		setVec2(pPartLink->verts[1].texcoord, 1, 0);
	}
	else if ( pDraw->bVInvert )
	{
		setVec2(pPartLink->verts[0].texcoord, 0, 0);
		setVec2(pPartLink->verts[1].texcoord, 0, 1);
		setVec2(pPartLink->verts[2].texcoord, 1, 1);
		setVec2(pPartLink->verts[3].texcoord, 1, 0);
	}
	else
	{
		setVec2(pPartLink->verts[1].texcoord, 0, 0);
		setVec2(pPartLink->verts[0].texcoord, 0, 1);
		setVec2(pPartLink->verts[3].texcoord, 1, 1);
		setVec2(pPartLink->verts[2].texcoord, 1, 0);
	}

	{
		Vec4 vShiftedColor;

		hsvShiftRGB(
			pDraw->vColor,
			vShiftedColor,
			pDraw->vHSVShift[0], 
			pDraw->vHSVShift[1],
			pDraw->vHSVShift[2]);

		vShiftedColor[3] = pDraw->vColor[3];

		if (pDraw->fHueShift || pDraw->fSaturationShift || pDraw->fValueShift)
		{
			Vec4 vColor;
			
			hsvShiftRGB(vShiftedColor, vColor,
				pDraw->fHueShift,
				pDraw->fSaturationShift,
				pDraw->fValueShift);

			vColor[3] = pDraw->vColor[3];
			scaleVec4(vColor, U8TOF32_COLOR, pPartLink->verts[0].color);
		}
		else
		{
			scaleVec4(vShiftedColor, U8TOF32_COLOR, pPartLink->verts[0].color);
		}
	}
	pPartLink->verts[0].color[3] *= pParticle->fFadeOut;
	
	// Handle light modulation.
	if(pParticle->pDraw->fLightModulation > 0) {
		Vec3 vLightColor;
		gfxCalcSimpleLightValueForPoint(world_mid, vLightColor);
		mulVecVec3(pPartLink->verts[0].color, vLightColor, vLightColor);
		interpVec3(pParticle->pDraw->fLightModulation, pPartLink->verts[0].color, vLightColor, pPartLink->verts[0].color);
	}

	copyVec4(pPartLink->verts[0].color, pPartLink->verts[1].color);
	copyVec4(pPartLink->verts[0].color, pPartLink->verts[2].color);
	copyVec4(pPartLink->verts[0].color, pPartLink->verts[3].color);

	switch ( pDraw->iStreakMode )
	{
		xcase DynStreakMode_None:
		{
			U32 uiPoint;

			setVec3(pPartLink->verts[0].point, -0.5f, -0.5f,  0.0f);
			setVec3(pPartLink->verts[1].point, -0.5f,  0.5f,  0.0f);
			setVec3(pPartLink->verts[2].point,  0.5f,  0.5f,  0.0f);
			setVec3(pPartLink->verts[3].point,  0.5f, -0.5f,  0.0f);

			for (uiPoint=0; uiPoint<4; ++uiPoint)
			{
				Vec3 vTemp;
                eDynOrientMode effectiveOrientation;

				setVec3(pPartLink->verts[uiPoint].normal, 0.0f, 0.0f, -1.0f);
				setVec3(pPartLink->verts[uiPoint].binormal, 0.0f, -1.0f, 0.0f);
				setVec3(pPartLink->verts[uiPoint].tangent, 1.0f, 0.0f, 0.0f);

				// scale point
				mulVecVec2(vScale, pPartLink->verts[uiPoint].point, pPartLink->verts[uiPoint].point);

				// Add tighten up 
				pPartLink->verts[uiPoint].tightenup = pDraw->fTightenUp;

                if( draw_params->wl_params.is_screen_space ) {
                    effectiveOrientation = DynOrientMode_Local;
                } else {
                    effectiveOrientation = pDraw->eOriented;
                }

				switch (effectiveOrientation)
				{
					xcase DynOrientMode_ZAxis:
					{
						Quat qRot;
						dynNodeGetWorldSpaceRot(&pDraw->node, qRot);
						quatRotateVec3ZOnly(qRot, pPartLink->verts[uiPoint].point, vTemp);
						mulVecMat3(vTemp, pass->frustum->inv_viewmat, pPartLink->verts[uiPoint].point);
					}
					xcase DynOrientMode_LockToX:
					{
						Quat qRot;
						Mat3 mLocalMat;
						Vec3 vXAxis = {1.0f, 0.0f, 0.0f};
						F32 fCosTheta = qcos(RAD(pDraw->fSpriteOrientation));
						F32 fSinTheta = qsin(RAD(pDraw->fSpriteOrientation));
						vTemp[0] = pPartLink->verts[uiPoint].point[0] * fCosTheta + pPartLink->verts[uiPoint].point[1] * fSinTheta;
						vTemp[1] = pPartLink->verts[uiPoint].point[1] * fCosTheta - pPartLink->verts[uiPoint].point[0] * fSinTheta;
						vTemp[2] = pPartLink->verts[uiPoint].point[2];
						dynNodeGetWorldSpaceRot(&pDraw->node, qRot);
						quatRotateVec3(qRot, vXAxis, mLocalMat[0]);
						crossVec3(mLocalMat[0], gdraw->visual_pass_view_z, mLocalMat[1]);
						normalVec3(mLocalMat[1]);
						crossVec3(mLocalMat[1], mLocalMat[0], mLocalMat[2]);
						normalVec3(mLocalMat[2]);
						mulVecMat3(vTemp, mLocalMat, pPartLink->verts[uiPoint].point);
					}
					xcase DynOrientMode_LockToY:
					{
						Quat qRot;
						Mat3 mLocalMat;
						Vec3 vYAxis = {0.0f, 1.0f, 0.0f};
						F32 fCosTheta = qcos(RAD(pDraw->fSpriteOrientation));
						F32 fSinTheta = qsin(RAD(pDraw->fSpriteOrientation));
						vTemp[0] = pPartLink->verts[uiPoint].point[0] * fCosTheta + pPartLink->verts[uiPoint].point[1] * fSinTheta;
						vTemp[1] = pPartLink->verts[uiPoint].point[1] * fCosTheta - pPartLink->verts[uiPoint].point[0] * fSinTheta;
						vTemp[2] = pPartLink->verts[uiPoint].point[2];
						dynNodeGetWorldSpaceRot(&pDraw->node, qRot);
						quatRotateVec3(qRot, vYAxis, mLocalMat[1]);
						crossVec3(mLocalMat[1], gdraw->visual_pass_view_z, mLocalMat[0]);
						normalVec3(mLocalMat[0]);
						crossVec3(mLocalMat[1], mLocalMat[0], mLocalMat[2]);
						normalVec3(mLocalMat[2]);
						mulVecMat3(vTemp, mLocalMat, pPartLink->verts[uiPoint].point);
					}
					xcase DynOrientMode_Local:
					{
						Quat qRot;
						F32 fCosTheta = qcos(RAD(pDraw->fSpriteOrientation));
						F32 fSinTheta = qsin(RAD(pDraw->fSpriteOrientation));
						vTemp[0] = pPartLink->verts[uiPoint].point[0] * fCosTheta + pPartLink->verts[uiPoint].point[1] * fSinTheta;
						vTemp[1] = pPartLink->verts[uiPoint].point[1] * fCosTheta - pPartLink->verts[uiPoint].point[0] * fSinTheta;
						vTemp[2] = pPartLink->verts[uiPoint].point[2];
						dynNodeGetWorldSpaceRot(&pDraw->node, qRot);
						quatRotateVec3(qRot, vTemp, pPartLink->verts[uiPoint].point);

						copyVec3(pPartLink->verts[uiPoint].normal, vTemp);
						quatRotateVec3(qRot, vTemp, pPartLink->verts[uiPoint].normal);
						copyVec3(pPartLink->verts[uiPoint].binormal, vTemp);
						quatRotateVec3(qRot, vTemp, pPartLink->verts[uiPoint].binormal);
						copyVec3(pPartLink->verts[uiPoint].tangent, vTemp);
						quatRotateVec3(qRot, vTemp, pPartLink->verts[uiPoint].tangent);
					}
					xcase DynOrientMode_Camera:
					{
						F32 fCosTheta = qcos(RAD(pDraw->fSpriteOrientation));
						F32 fSinTheta = qsin(RAD(pDraw->fSpriteOrientation));
						vTemp[0] = pPartLink->verts[uiPoint].point[0] * fCosTheta + pPartLink->verts[uiPoint].point[1] * fSinTheta;
						vTemp[1] = pPartLink->verts[uiPoint].point[1] * fCosTheta - pPartLink->verts[uiPoint].point[0] * fSinTheta;
						vTemp[2] = pPartLink->verts[uiPoint].point[2];
						mulVecMat3(vTemp, pass->frustum->inv_viewmat, pPartLink->verts[uiPoint].point);
					}

				};


				// translate point
				addVec3(world_mid, pPartLink->verts[uiPoint].point, pPartLink->verts[uiPoint].point);
			}

		}
		xcase DynStreakMode_Velocity:
		case DynStreakMode_Parent:
		case DynStreakMode_Chain:
		case DynStreakMode_ScaleToTarget:
		{
			U32 uiPoint;
			F32 fStreakLength = 0;
			Vec3 vAlongScreen;
			Vec3 vTemp[4];

			switch (pDraw->eOriented)
			{
				xcase DynOrientMode_Local:
				{
					Vec2 vStreakDir = { pDraw->vStreakDir[0], pDraw->vStreakDir[2]};
					F32 fNormVec2 = normalVec2(vStreakDir);
					Vec2 vNormOrthStreakDir = { 0.0f, 0.0f};
					if (fNormVec2 > 0.0f)
					{
						vNormOrthStreakDir[0] = -vStreakDir[1] / fNormVec2;
						vNormOrthStreakDir[1] =  vStreakDir[0] / fNormVec2;
					}

					scaleVec2(vNormOrthStreakDir, vScale[0], vNormOrthStreakDir);

					for (uiPoint=0; uiPoint<4; ++uiPoint)
					{
						// Add tighten up 
						pPartLink->verts[uiPoint].tightenup = pDraw->fTightenUp;

						// translate point
						copyVec3(world_mid, pPartLink->verts[uiPoint].point);

						if ( uiPoint == 0 || uiPoint == 3)
						{
							addVec3(pPartLink->verts[uiPoint].point, pDraw->vStreakDir, pPartLink->verts[uiPoint].point);
						}
						if (uiPoint == 0 || uiPoint == 1)
						{
							pPartLink->verts[uiPoint].point[0] += vNormOrthStreakDir[0];
							pPartLink->verts[uiPoint].point[2] += vNormOrthStreakDir[1];
						}
						else
						{
							pPartLink->verts[uiPoint].point[0] -= vNormOrthStreakDir[0];
							pPartLink->verts[uiPoint].point[2] += vNormOrthStreakDir[1];
						}
					}
				}
				xdefault:
				{
					mulVecMat3(pDraw->vStreakDir, pass->viewmat, vAlongScreen);

					vAlongScreen[2] = 0.0f;
					normalVec2(vAlongScreen);
					mulVecVec2(vAlongScreen, vScale, vAlongScreen);

					setVec3(vTemp[3], -vAlongScreen[1] * 0.5f, vAlongScreen[0] * 0.5f, 0.0f);
					setVec3(vTemp[0], vAlongScreen[1] * 0.5f, -vAlongScreen[0] * 0.5f, 0.0f);

					setVec3(vTemp[2], -vAlongScreen[1] * 0.5f, vAlongScreen[0] * 0.5f, 0.0f);
					setVec3(vTemp[1], vAlongScreen[1] * 0.5f, -vAlongScreen[0] * 0.5f, 0.0f);

					for (uiPoint=0; uiPoint<4; ++uiPoint)
					{
						// rotate point into world space
						mulVecMat3(vTemp[uiPoint], pass->frustum->inv_viewmat, pPartLink->verts[uiPoint].point);

						// Add tighten up 
						pPartLink->verts[uiPoint].tightenup = pDraw->fTightenUp;

						// translate point
						addVec3(world_mid, pPartLink->verts[uiPoint].point, pPartLink->verts[uiPoint].point);

						if ( uiPoint == 0 || uiPoint == 3)
						{
							addVec3(pPartLink->verts[uiPoint].point, pDraw->vStreakDir, pPartLink->verts[uiPoint].point);
						}
					}
				}
			};
			if (pDraw->bStreakTile)
			{
				Vec3 vDiff;
				F32 fDist;
				subVec3(pPartLink->verts[0].point, pPartLink->verts[1].point, vDiff);
				fDist = lengthVec3(vDiff);
				pPartLink->verts[0].texcoord[1] *= fDist;
				pPartLink->verts[1].texcoord[1] *= fDist;
				pPartLink->verts[2].texcoord[1] *= fDist;
				pPartLink->verts[3].texcoord[1] *= fDist;
			}
		}
	}

	if( draw_params->wl_params.is_screen_space && pDraw->bFixedAspectRatio ) {

		// Sprite needs to be converted to a fixed aspect ratio.
		float sx = 1;
		float sy = 1;
		F32 aspect = gfxGetAspectRatio();
		int i;

		if(aspect > 1) {
			// X is larger.
			sx /= aspect;
		} else {
			// Y is larger.
			sy *= aspect;
		}

		for(i = 0; i < 4; i++) {
			pPartLink->verts[i].point[0] = (pPartLink->verts[i].point[0] - 0.5) * sx + 0.5;
			pPartLink->verts[i].point[1] = (pPartLink->verts[i].point[1] - 0.5) * sy + 0.5;
		}
	}

	{
		Vec4_aligned eye_bounds[8];
		Vec3 world_min, world_max;
		int i;
		copyVec3(pPartLink->verts[0].point, world_min);
		copyVec3(pPartLink->verts[0].point, world_max);
		for (i = 1; i < 4; ++i)
		{
			vec3RunningMin(pPartLink->verts[i].point, world_min);
			vec3RunningMax(pPartLink->verts[i].point, world_max);
		}
		mulBounds(world_min, world_max, gdraw->visual_pass->viewmat, eye_bounds);
		gfxGetScreenSpace(gdraw->visual_pass->frustum, gdraw->nextVisProjection, 1, eye_bounds, &instance_params.screen_area);
	}

	if (bCreatedDrawable)
		rdrDrawListAddParticle(draw_list, pPartDraw, &instance_params, RST_AUTO, draw_params->wl_params.is_costume?ROC_CHARACTER:ROC_FX, zdist);
}


Vec2 fastTexCoord[4] =
{
	{ 0.0f, 1.0f },
	{ 0.0f, 0.0f },
	{ 1.0f, 0.0f },
	{ 1.0f, 1.0f },
};

Vec3 fastPoints[4] =
{
	{ -0.5f, -0.5f,  0.0f },
	{ -0.5f,  0.5f,  0.0f },
	{  0.5f,  0.5f,  0.0f },
	{  0.5f, -0.5f,  0.0f },
};

const U32 uiNoiseSize = 4096;

static BasicTexture *noiseTextureCreate(void)
{
	static BasicTexture* hNoiseTexture = NULL;
	F32 *pData;

	if (hNoiseTexture)
		return hNoiseTexture;
	hNoiseTexture = texGenNew(uiNoiseSize, 1, "FastParticleNoise", TEXGEN_NORMAL, WL_FOR_FX);

	pData = memrefAlloc(sizeof(F32)*uiNoiseSize);
	{
		U32 i;
		for (i=0; i<uiNoiseSize; ++i)
		{
			pData[i] = randomMersenneF32(NULL);
//			xbEndianSwapF32(pData[i]);
		}
	}
	texGenUpdate(hNoiseTexture, (U8*)pData, RTEX_2D, RTEX_R_F32, 1, true, false, true, true);
	memrefDecrement(pData);

	// The device is locked and we're in the middle of drawing, but make sure this gets loaded!
	if(gfx_state.currentDevice->rdr_device->is_locked_nonthread)
		texGenDoFrame();

	return hNoiseTexture;
}

void gfxSetSoftParticleDepthTexture(RdrDevice *rdr_device, TexHandle depth_buffer)
{
	if (!rdr_device || !rdr_device->is_locked_nonthread)
		return;
	rdrSetSoftParticleDepthTexture(gfx_state.currentDevice->rdr_device, depth_buffer);
}

void gfxQueueSingleFastParticleSet(DynFxFastParticleSet* pSet, const DynDrawParams* draw_params, F32 fFadeOut)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int iNumFastParticles;
	DynFxFastParticleInfo* pInfo = GET_REF(pSet->hInfo);
	bool isInScreenSpace = (draw_params?draw_params->wl_params.is_screen_space:false);
	Vec3 vPos;
	int i;

	if (gfx_state.currentDevice->skipThisFrame)
		return;

	PERFINFO_AUTO_START_FUNC_PIX();

	// Handle any delayed fast particle updates right now.
	if(pSet->delayedUpdate.bDelayedUpdate) {
		dynFxFastParticleSetUpdate(
			pSet, pSet->delayedUpdate.vWorldSpaceVelocity,
			pSet->delayedUpdate.fDeltaTime, false,
			pSet->delayedUpdate.bVisible);
	}

	iNumFastParticles = dynFxFastParticleSetGetNumParticles(pSet);

	addVec3(pSet->vPos, gdraw->pos_offset, vPos);

	if (iNumFastParticles > 0 && pInfo && fFadeOut > 0.0f)
	{
		bool bStreak = pInfo->eStreakMode != DynFastStreakMode_None;
		RdrDrawableFastParticles* pRdrSet = rdrDrawListAllocFastParticles(gdraw->draw_list, RTYPE_FASTPARTICLES, iNumFastParticles, bStreak, MAX(pSet->uiNumAtNodes, 1));
		PoolQueueIterator iter;
		DynFxFastParticle* pFastParticle;
		S32 iIndex;
		Vec3 vPrevPos;
		RdrAddFastParticleParams params={0};

		if (!pRdrSet)
			return;

		dynDebugState.frameCounters.uiNumDrawnFastParticles += iNumFastParticles;
		++dynDebugState.frameCounters.uiNumDrawnFastParticleSets;

		if (SAFE_MEMBER(draw_params, wl_params.is_costume))
			++dynDebugState.frameCounters.uiNumDrawnCostumeFx;
		else if (SAFE_MEMBER(draw_params, wl_params.is_debris))
			++dynDebugState.frameCounters.uiNumDrawnDebris;
		else
		{
			++dynDebugState.frameCounters.uiNumDrawnFx;
			if (dynDebugState.bFxDebugOn)
			{
				DynDrawTracker* pDrawTracker = dynDrawTrackerPush();
				pDrawTracker->pcName = pSet->pcEmitterName;
				pDrawTracker->ePriority = pSet->iPriorityLevel;
				pDrawTracker->uiSubObjects = iNumFastParticles;
				pDrawTracker->eType = eDynFxType_FastParticleSet;
				pDrawTracker->uiNum = 1;
				if (dynDebugState.bFxDrawOnlySelected)
				{
					bool bDraw = false;
					FOR_EACH_IN_EARRAY(eaDynDrawOnly, const char, pcDrawOnly)
					{
						if (pcDrawOnly == pSet->pcEmitterName)
						{
							bDraw = true;
							break;
						}
					}
					FOR_EACH_END;

					if (!bDraw)
						return;
				}
			}
		}

		pRdrSet->soft_particles = gfx_state.settings.soft_particles;
		pRdrSet->is_screen_space = isInScreenSpace;
		pRdrSet->animated_texture = pInfo ? pInfo->bAnimatedTexture : false;

		pRdrSet->special_params[0] = pSet->bOverrideSpecialParam ? 0 : gfx_state.project_special_material_param;

#if _PS3
		// the following code is an optimized version of the code to build up a vertex buffer for the particles
		// the focus of the optimization was to as much branching out of the inner loop as possible.  This almost
		// cuts the 1.7ms per frame spent in here in half.
		{
			int	oldestFirstIncrement = pInfo->bOldestFirst? -8 : 0;
	
			// create vector for optional poistion offset
			Vec3 pos_offset;
			if (pSet->ePosFlag == DynParticleEmitFlag_Update)
				copyVec3(gdraw->pos_offset, pos_offset);
			else
				setVec3(pos_offset, 0.0f, 0.0f, 0.0f);

			poolQueueGetIterator(&pSet->particleQueue, &iter);

			if (bStreak)
			{
				RdrFastParticleStreakVertex* pVerts = pRdrSet->streakverts;

				// we may go through list of particles backwards
				if (pInfo->bOldestFirst)
					pVerts += (iNumFastParticles-1)*4;

				while (poolQueueGetNextElement(&iter, &pFastParticle))
				{
					float	alpha = pFastParticle->bInvisible?0.0f:1.0f;
					Vec3 vFastParticlePos;

					addVec3(pFastParticle->vPos, pos_offset, vFastParticlePos);

					for (i=0; i<4; ++i)
					{
						pVerts->seed = pFastParticle->fSeed;
						copyVec3(vFastParticlePos, pVerts->point);
						pVerts->time = pFastParticle->fTime;
						pVerts->corner_nodeidx[0] = (S16)i;
						pVerts->alpha = alpha;
						pVerts->corner_nodeidx[1] = pFastParticle->uiNodeIndex;
						switch( pInfo->eStreakMode )
						{
							xcase DynFastStreakMode_Velocity:
								scaleVec3(pFastParticle->vVel, -1.0f, pVerts->streak_dir);
							xcase DynFastStreakMode_VelocityNoScale:
								{
									Vec3 vNoScaleVel;
									copyVec3(pFastParticle->vVel, vNoScaleVel);
									normalVec3(vNoScaleVel);
									scaleVec3(vNoScaleVel, -1.0f, pVerts->streak_dir);
								}
							xcase DynFastStreakMode_Chain:
								if (pVerts < pRdrSet->streakverts + 4 || pFastParticle->bFirstInLine)
								{
									copyVec3(zerovec3, pVerts->streak_dir);
									pVerts->corner_nodeidx[0] = 0;
								}
								else
									subVec3(vPrevPos, vFastParticlePos, pVerts->streak_dir);
								if (i==3)
									copyVec3( vFastParticlePos, vPrevPos );
							xcase DynFastStreakMode_Parent:
								if (pSet->ePosFlag == DynParticleEmitFlag_Update)
									scaleVec3(vFastParticlePos, -1.0f, pVerts->streak_dir);
								else
									subVec3(vPos, vFastParticlePos, pVerts->streak_dir);
						};

						++pVerts;
					}

					pVerts += oldestFirstIncrement;		// possibly jump back to the previous set of 4
				}
			}
			else
			{
				RdrFastParticleVertex *pVerts = pRdrSet->verts;
				if (pInfo->bOldestFirst)
					pVerts += (iNumFastParticles-1)*4;

				while (poolQueueGetNextElement(&iter, &pFastParticle))
				{
					float	alpha = pFastParticle->bInvisible?0.0f:1.0f;
					Vec3 vFastParticlePos;

					addVec3(pFastParticle->vPos, pos_offset, vFastParticlePos);

					vFastParticlePos[1] += 0.0000001f; // this hack is to fix a bug where objects in the exact center of the screen don't render on PC, for no apparent reason -SAM
					for (i=0; i<4; ++i)
					{
						pVerts->alpha = alpha;
						pVerts->seed = pFastParticle->fSeed;
						copyVec3(vFastParticlePos, pVerts->point);
						pVerts->time = pFastParticle->fTime;
						pVerts->corner_nodeidx[0] = (S16)i;
						pVerts->corner_nodeidx[1] = pFastParticle->uiNodeIndex;

						pVerts++;
					}

					pVerts += oldestFirstIncrement;		// possibly jump back to the previous set of 4
				}
			}
		}
#else
		if (pInfo->bOldestFirst)
			iIndex = (iNumFastParticles-1)*4;
		else
			iIndex = 0;

		poolQueueGetIterator(&pSet->particleQueue, &iter);
		while (poolQueueGetNextElement(&iter, &pFastParticle))
		{
			Vec3 vFastParticlePos;
			assert(iIndex >= 0 && iIndex < iNumFastParticles*4);
			if (pSet->ePosFlag == DynParticleEmitFlag_Update)
				copyVec3(pFastParticle->vPos, vFastParticlePos);
			else
				addVec3(pFastParticle->vPos, gdraw->pos_offset, vFastParticlePos);
			for (i=0; i<4; ++i)
			{
				if (bStreak)
				{
					pRdrSet->streakverts[iIndex].seed = pFastParticle->fSeed;
					//addVec3(fastPoints[i], vFastParticlePos, pRdrSet->verts[iIndex].point);
					copyVec3(vFastParticlePos, pRdrSet->streakverts[iIndex].point);
					pRdrSet->streakverts[iIndex].time = pFastParticle->fTime;
					pRdrSet->streakverts[iIndex].corner_nodeidx[0] = (S16)i;
					pRdrSet->streakverts[iIndex].alpha = pFastParticle->bInvisible?0.0f:1.0f;
					pRdrSet->streakverts[iIndex].corner_nodeidx[1] = pFastParticle->uiNodeIndex;
					switch( pInfo->eStreakMode )
					{
						xcase DynFastStreakMode_Velocity:
							scaleVec3(pFastParticle->vVel, -1.0f, pRdrSet->streakverts[iIndex].streak_dir);
						xcase DynFastStreakMode_VelocityNoScale:
							{
								Vec3 vNoScaleVel;
								copyVec3(pFastParticle->vVel, vNoScaleVel);
								normalVec3(vNoScaleVel);
								scaleVec3(vNoScaleVel, -1.0f, pRdrSet->streakverts[iIndex].streak_dir);
							}
						xcase DynFastStreakMode_Chain:
							if (iIndex < 4 || pFastParticle->bFirstInLine)
							{
								copyVec3(zerovec3, pRdrSet->streakverts[iIndex].streak_dir);
								pRdrSet->streakverts[iIndex].corner_nodeidx[0] = 0;
							}
							else
								subVec3(vPrevPos, vFastParticlePos, pRdrSet->streakverts[iIndex].streak_dir);
							if (i==3)
								copyVec3( vFastParticlePos, vPrevPos );
						xcase DynFastStreakMode_Parent:
							if (pSet->ePosFlag == DynParticleEmitFlag_Update)
								scaleVec3(vFastParticlePos, -1.0f, pRdrSet->streakverts[iIndex].streak_dir);
							else
								subVec3(vPos, vFastParticlePos, pRdrSet->streakverts[iIndex].streak_dir);
					};
				}
				else
				{
					pRdrSet->verts[iIndex].alpha = pFastParticle->bInvisible?0.0f:1.0f;
					pRdrSet->verts[iIndex].seed = pFastParticle->fSeed;
					//addVec3(fastPoints[i], vFastParticlePos, pRdrSet->verts[iIndex].point);
					copyVec3(vFastParticlePos, pRdrSet->verts[iIndex].point);
					pRdrSet->verts[iIndex].point[1] += 0.0000001f; // this hack is to fix a bug where objects in the exact center of the screen don't render on PC, for no apparent reason -SAM
					pRdrSet->verts[iIndex].time = pFastParticle->fTime;
					pRdrSet->verts[iIndex].corner_nodeidx[0] = (S16)i;
					pRdrSet->verts[iIndex].corner_nodeidx[1] = pFastParticle->uiNodeIndex;
				}
				++iIndex;
			}
			if (pInfo->bOldestFirst)
			{
				iIndex -= 8; // jump back to the previous set of 4
			}
		}
#endif
		if (pSet->uiNumAtNodes == 0)
		{
			if (pSet->eRotFlag == DynParticleEmitFlag_Update)
				quatToMat34Inline(pSet->qRot, pRdrSet->bone_infos[0][0]);
			else
				copyMat34(unitmat44, pRdrSet->bone_infos[0]);
			setMat34Col(pRdrSet->bone_infos[0], 3,
				pSet->ePosFlag != DynParticleEmitFlag_Inherit ? vPos : zerovec3);
		}
		else
		{
			// Need to update to at nodes in the vertex shader, so set up skinning mats
			DynFx* pParentFx = GET_REF(pSet->hParentFX);
			if (pParentFx)
			{
				U32 uiNodeIndex;
				for (uiNodeIndex=0; uiNodeIndex<pSet->uiNumAtNodes; ++uiNodeIndex)
				{
					const DynNode* pAtNode = dynFxFastParticleSetGetAtNode(pSet, uiNodeIndex);
					Mat4 mAtMat;
					bool bGotAtMat = false;
					if (pAtNode)
					{
						if (pSet->eRotFlag == DynParticleEmitFlag_Update)
						{
							dynNodeGetWorldSpaceMat(pAtNode, mAtMat, false);
							mat4toSkinningMat4(mAtMat, pRdrSet->bone_infos[uiNodeIndex]);
							bGotAtMat = true;
						}
						else
							copyMat34(unitmat44, pRdrSet->bone_infos[uiNodeIndex]);
						if (pSet->ePosFlag != DynParticleEmitFlag_Inherit)
						{
							if (!bGotAtMat)
								dynNodeGetWorldSpaceMat(pAtNode, mAtMat, false);
							else
								dynNodeGetWorldSpacePos(pAtNode, mAtMat[3]);
							addVec3(mAtMat[3], gdraw->pos_offset, mAtMat[3]);
							//copyVec3(mAtMat[3], pRdrSet->bone_infos[ipcAtNodeIndex][3]);
							setMat34Col(pRdrSet->bone_infos[uiNodeIndex], 3, mAtMat[3]);
						}
						else
							setMat34Col(pRdrSet->bone_infos[uiNodeIndex], 3, zerovec3);
					}
					else
					{
						copyMat34(unitmat44, pRdrSet->bone_infos[uiNodeIndex]);
					}
				}
			}
			else  // better hope it's inherit!
			{
				U32 uiNodeIndex;
				for (uiNodeIndex=0; uiNodeIndex<pSet->uiNumAtNodes; ++uiNodeIndex)
				{
					copyMat34(unitmat44, pRdrSet->bone_infos[uiNodeIndex]);
				}
			}

		}

		memcpy(pRdrSet->constants, &pInfo->compiled, sizeof(pRdrSet->constants));
		pRdrSet->time_info[0] = pSet->fSetTime;
		pRdrSet->time_info[1] = pInfo->fLifeSpanInv;
		pRdrSet->time_info[2] = pSet->fHueShift;
		pRdrSet->time_info[3] = fFadeOut;
		pRdrSet->scale_info[0] = (1.0 - pSet->fScaleSprite) + pSet->vScale[0] * pSet->fScaleSprite;
		pRdrSet->scale_info[1] = (1.0 - pSet->fScaleSprite) + pSet->vScale[1] * pSet->fScaleSprite;

		// Linkscale does not work with fixed aspect ratio because the flag will force the
		// shader to use only one of the scale values.
		if(isInScreenSpace && !pInfo->bLinkScale) {
			if(dynFxFastParticleUseConstantScreenSize(pSet)) {
				float aspect = gfxGetAspectRatio();
				if(aspect > 1.0f) {
					pRdrSet->scale_info[0] /= aspect;
				} else {
					pRdrSet->scale_info[1] *= aspect;
				}
			}
		}

		pRdrSet->scale_info[2] = pInfo->bConstantScreenSize?1.0f:0.0f;
		pRdrSet->scale_info[3] = isInScreenSpace?15000:(-FAST_PARTICLE_CUTOUT_DEPTH * pSet->fCutoutDepthScale);

		pRdrSet->hsv_info[0] = pSet->fHueShift;
		pRdrSet->hsv_info[1] = pSet->fSaturationShift;
		pRdrSet->hsv_info[2] = pSet->fValueShift;
		pRdrSet->hsv_info[3] = 0;

		copyVec4(pSet->vModulateColor, pRdrSet->modulate_color);
		
		pRdrSet->blendmode =  dynNodeGetRdrBlendFlags(pInfo->eBlendMode);
		pRdrSet->no_tonemap = pInfo->bNoToneMap;
		pRdrSet->link_scale = pInfo->bLinkScale;
		pRdrSet->streak = bStreak;
		pRdrSet->rgb_blend = pInfo->bRGBBlend;

		if (isInScreenSpace)
		{
			params.zdist = -pInfo->fSortBias;
			params.zbias = 10000;
		} else {
			params.zdist = rdrDrawListCalcZDist(gdraw->draw_list, vPos) - pInfo->fSortBias;
			params.zbias = pInfo->fZBias;
		}

		if (!pInfo->pTexture)
			pInfo->pTexture = texLoadBasic(pInfo->pcTexture, TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);
		pRdrSet->tex_handle = texDemandLoadInline(pInfo->pTexture, isInScreenSpace?1.f:params.zdist, UV_DENSITY_FOR_FAST_PARTICLES, white_tex);
		if( rdrSupportsFeature( gfx_state.currentDevice->rdr_device, FEATURE_VFETCH ))
			pRdrSet->noise_tex = texDemandLoadFixed(noiseTextureCreate());

		params.wireframe = dynDebugState.bFxNotWireframe?0:gdraw->global_wireframe;

		{
			Vec4_aligned eye_bounds[8], world_min, world_max;
			addVec3same(vPos, pInfo->fRadius, world_max);
			subVec3same(vPos, pInfo->fRadius, world_min);
			mulBounds(world_min, world_max, gdraw->visual_pass->viewmat, eye_bounds);
			gfxGetScreenSpace(gdraw->visual_pass->frustum, gdraw->nextVisProjection, 1, eye_bounds, &params.screen_area);
		}

		for (i = 0; i < ARRAY_SIZE(pInfo->curvePath); ++i)
		{
			// color is in HSV space
			MAX1(params.brightness, pInfo->curvePath[i].vColor[2] + pInfo->curveJitter[i].vColor[2]);
		}

		params.uses_far_depth_range = gdraw->use_far_depth_range;
		if (pInfo->bLowRes)
			pRdrSet->blendmode |= RMATERIAL_LOW_RES_ALPHA;
		rdrDrawListAddFastParticles(gdraw->draw_list, pRdrSet, &params, RST_ALPHA, SAFE_MEMBER(draw_params, wl_params.is_costume)?ROC_CHARACTER:ROC_FX);
	}

	PERFINFO_AUTO_STOP_FUNC_PIX();
}

static F32 calculateTrailTexCoord(DynMeshTrail* pMeshTrail, DynMeshTrailUnit* pUnit, U32 uiPointIndex, U32 uiNumPoints)
{
	// If no texdensity is set, the texture is stretched over the whole meshtrail
	if (pMeshTrail->meshTrailInfo.fTexDensity <= 0.0f)
	{
		F32 fTexCoord = 1.0f;
		if (uiPointIndex==0)
			fTexCoord = 0.0f;
		else if (uiPointIndex+1 < uiNumPoints)
		{
			F32 fFraction;
			F32 fNumerator;
			F32 fDivisor;
			fFraction = 1.0f - pMeshTrail->fAccum * pMeshTrail->meshTrailInfo.fEmitRate;
			MAX1(fFraction, 0.001f);
			fDivisor = (F32)uiNumPoints - 2.0f + fFraction;
			fNumerator = fFraction + ((F32)uiPointIndex-1.0f);
			fTexCoord = fNumerator / fDivisor;
		}
		fTexCoord = CLAMP(fTexCoord, 0.0f, 1.0f);
		return fTexCoord;
	}

	// otherwise, it's tiled, and we've precalculated the coords
	return -pUnit->fTexCoord;
}

static void gfxQueueMeshTrail(DynParticle *pMeshParticle, const DynDrawParams *draw_params)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	Vec3 vPartPos;
	DynDrawParticle* pDraw = pMeshParticle->pDraw;
	DynMeshTrail* pMeshTrail = pDraw->pMeshTrail;
	DynMeshTrailInfo* pInfo = &pMeshTrail->meshTrailInfo;
	F32 maxWidthEst = MAX(pInfo->keyFrames[0].fWidth, pInfo->keyFrames[1].fWidth); // There can be more key frames, but we just need a really rough approximation here
	F32 fUvDensity = -log2f(maxWidthEst);	//0.5*(log2(1/(maxWidthEst*maxWidthEst)))
	U32 uiNumPoints = poolQueueGetNumElements(pMeshTrail->qUnits);
	RdrDrawableCylinderTrail *pCylTrail;
	RdrDrawableTriStrip *pTriStrip;
	RdrAddInstanceParams instance_params={0};
	RdrInstancePerDrawableData per_drawable_data;
	U32 uiPointIndex=0;
	PoolQueueIterator iter;
	DynMeshTrailUnit* pUnit;
	DynMeshTrailUnit* pPrevUnit = NULL;
	DynMeshTrailUnit* pNextUnit;
	U8 uiNumKeyFrames=pMeshTrail->uiNumKeyFrames;
	U8 uiCurrentKeyFrame=0;
	U8 uiNextKeyFrame=1;
	bool bCylinder = (pInfo->mode == DynMeshTrail_Cylinder);
	const U32 uiNumCylinderSegments = 8; // how many faces per ring
	const U32 uiNumCylinderVertsPerSegment = uiNumCylinderSegments+1; // how many points per ring
	RdrDrawListPassData *pass = gdraw->visual_pass;
	Vec3 vWorldMin={8e16,8e16,8e16}, vWorldMax={-8e16,-8e16,-8e16};
	Vec3 vPartScale;
	MaterialNamedConstant ***peaMNC = &spriteNamedConstants;
	RdrDrawList *draw_list = gdraw->draw_list;

	if (!pass || !(draw_params->frustum_visible & pass->frustum_set_bit))
		return;

	if (uiNumKeyFrames < 2 || uiNumPoints < 2)
		return;

	if (draw_params->wl_params.is_screen_space) {
		draw_list = gfx_state.currentDevice->draw_list2d;
	
		// HACK - Bail out of drawing screen space
		// particles if the 2D draw list isn't set up yet.
		// -Cliff
		if(!rdrDrawListHasData(draw_list)) return;
	}

	if ( bCylinder )
	{
		U32 uiNumCylVertices = uiNumPoints * uiNumCylinderVertsPerSegment;
		// 6 indices per face, one face per segment per ring, (numPoints - 1) rings
		U32 uiNumCylIndices = 6 * uiNumCylinderSegments * (uiNumPoints-1);
		pCylTrail = rdrDrawListAllocCylinderTrail(draw_list, RTYPE_CYLINDERTRAIL, uiNumCylVertices, uiNumCylIndices, uiNumPoints*3);
		if (!pCylTrail)
			return;
		++dynDebugState.frameCounters.uiNumDrawnCylinderTrails;
		pCylTrail->tex_handle0 = pDraw->hTexHandle;
		pCylTrail->tex_handle1 = pDraw->hTexHandle2;
		if (pMeshParticle->pDraw->iBlendMode == DynBlendMode_Additive)
			pCylTrail->add_material_flags |= RMATERIAL_ADDITIVE;
		if (pMeshParticle->pDraw->iBlendMode == DynBlendMode_Subtractive)
			pCylTrail->add_material_flags |= RMATERIAL_SUBTRACTIVE;
	}
	else
	{
		pTriStrip = rdrDrawListAllocTriStrip(draw_list, RTYPE_TRISTRIP, uiNumPoints * 2);
		if (!pTriStrip)
			return;
		++dynDebugState.frameCounters.uiNumDrawnTriStrips;
		pTriStrip->tex_handle0 = pDraw->hTexHandle;
		pTriStrip->tex_handle1 = pDraw->hTexHandle2;
		if (pMeshParticle->pDraw->iBlendMode == DynBlendMode_Additive)
			pTriStrip->add_material_flags |= RMATERIAL_ADDITIVE;
		if (pMeshParticle->pDraw->iBlendMode == DynBlendMode_Subtractive)
			pTriStrip->add_material_flags |= RMATERIAL_SUBTRACTIVE;
		pTriStrip->is_screen_space = draw_params->wl_params.is_screen_space;
	}
	
	if (draw_params->wl_params.is_costume)
		++dynDebugState.frameCounters.uiNumDrawnCostumeFx;
	else if (draw_params->wl_params.is_debris)
		++dynDebugState.frameCounters.uiNumDrawnDebris;
	else
	{
		++dynDebugState.frameCounters.uiNumDrawnFx;
		if (dynDebugState.bFxDebugOn)
		{
			DynDrawTracker* pDrawTracker = dynDrawTrackerPush();
			DynFxInfo* pFxInfo = draw_params->wl_params.pInfo;
			pDrawTracker->pcName = pFxInfo->pcDynName;
			pDrawTracker->ePriority = draw_params->wl_params.iPriorityLevel;
			pDrawTracker->uiSubObjects = uiNumPoints;
			pDrawTracker->eType = eDynFxType_MeshTrail;
			pDrawTracker->uiNum = 1;
			if (dynDebugState.bFxDrawOnlySelected)
			{
				bool bDraw = false;
				FOR_EACH_IN_EARRAY(eaDynDrawOnly, const char, pcDrawOnly)
				{
					if (pcDrawOnly == pFxInfo->pcDynName)
					{
						bDraw = true;
						break;
					}
				}
				FOR_EACH_END;

				if (!bDraw)
					return;
			}
		}
	}

	dynNodeGetWorldSpaceScale(&pDraw->node, vPartScale);

	// Iterate through the queue of trail points, and create the triangles (verts+colors) to draw
	poolQueueGetBackwardsIterator(pMeshTrail->qUnits, &iter);
	if ( !poolQueueGetNextElement(&iter, &pUnit) )
		pUnit = NULL;
	while (pUnit  && uiNextKeyFrame < uiNumKeyFrames )
	{
		F32 fAge = pMeshTrail->fTrailAge - pUnit->fAge;
		if ( !poolQueueGetNextElement(&iter, &pNextUnit))
			pNextUnit = NULL;

		// First, check to see if we need to update the keyframes for interpolation
		while (uiNextKeyFrame < uiNumKeyFrames && fAge >= pInfo->keyFrames[uiNextKeyFrame].fTime )
		{
			++uiNextKeyFrame;
		}
		assert(uiNextKeyFrame > 0);
		if ( uiNextKeyFrame >= uiNumKeyFrames )
			uiNextKeyFrame = uiNumKeyFrames-1;
		uiCurrentKeyFrame = uiNextKeyFrame-1;

		{
			F32 fWidth = 1.0f;
			Vec4 vColor = {1.0f, 1.0f, 1.0f, 1.0f};
			Vec3 vOrientation = {0.0f, 1.0f, 0.0f};
			Vec3 vToAdd;
			Vec3 vUnitForward;
			F32 fInterpParam = calcInterpParam(fAge, pInfo->keyFrames[uiCurrentKeyFrame].fTime, pInfo->keyFrames[uiNextKeyFrame].fTime);
			fInterpParam = CLAMP(fInterpParam, 0.0f, 1.0f);

			// Calc the width and the color from the keyframes
			fWidth = interpF32(fInterpParam, pInfo->keyFrames[uiCurrentKeyFrame].fWidth, pInfo->keyFrames[uiNextKeyFrame].fWidth) * vPartScale[0];
			interpVec4(fInterpParam, pInfo->keyFrames[uiCurrentKeyFrame].vColor, pInfo->keyFrames[uiNextKeyFrame].vColor, vColor);

			if (pDraw->fHueShift || pDraw->fSaturationShift || pDraw->fValueShift) {
				hsvShiftRGB(vColor, vColor, pDraw->fHueShift, pDraw->fSaturationShift, pDraw->fValueShift);
			}
			scaleVec4(vColor, U8TOF32_COLOR, vColor);

			vColor[3] *= pMeshParticle->fFadeOut;

			if ( pInfo->fFadeInTime > 0.0f )
			{
				F32 fFadeInMult = (pUnit->fAge - pMeshTrail->fEmitStartAge) / pInfo->fFadeInTime;
				if (fFadeInMult >= 0.0f && fFadeInMult < 1.0f)
					vColor[3] *= fFadeInMult;
			}

			if ( pPrevUnit && pNextUnit )
			{
				// In theory, we should normalize the vAB and vBC so they are equally weighted, but I'm guessing we can skip it
				Vec3 vAB, vBC;
				subVec3( pUnit->vPos, pPrevUnit->vPos, vAB );
				subVec3( pNextUnit->vPos, pUnit->vPos, vBC );
				addVec3( vAB, vBC, vUnitForward );
			}
			else if ( pPrevUnit )
			{
				subVec3( pUnit->vPos, pPrevUnit->vPos, vUnitForward );
			}
			else if ( pNextUnit )
			{
				subVec3( pNextUnit->vPos, pUnit->vPos, vUnitForward );
			}
			else
			{
				pPrevUnit = pUnit;
				pUnit = pNextUnit;
				continue;
			}

			if (vec3IsZero(vUnitForward))
			{
				pPrevUnit = pUnit;
				pUnit = pNextUnit;
				continue;
			}

			if ( !bCylinder )
			{
				F32 fTexCoord = calculateTrailTexCoord(pMeshTrail, pUnit, uiPointIndex, uiNumPoints);
				Vec3 vNormal, vBinormal, vTangent;

				if ( pInfo->mode == DynMeshTrail_CamOriented )
				{
					Vec3 vFromCamera;
					subVec3(pUnit->vPos, gdraw->cam_pos, vFromCamera);
					normalVec3(vFromCamera);
					normalVec3(vUnitForward);

					crossVec3(vFromCamera, vUnitForward, vOrientation);
					normalVec3(vOrientation);

					copyVec3(vUnitForward, vBinormal);
					copyVec3(vOrientation, vTangent);
					crossVec3(vTangent, vBinormal, vNormal);
				}
				else if ( pInfo->mode == DynMeshTrail_Normal )
				{
					copyVec3(pUnit->vOrientation, vOrientation);
					copyVec3(vUnitForward, vBinormal);
					copyVec3(vOrientation, vTangent);
					crossVec3(vTangent, vBinormal, vNormal);
					scaleVec3(vNormal, -1, vNormal);
				}


				assert(uiPointIndex < uiNumPoints);
				assert(uiPointIndex*2+1 < pTriStrip->vert_count);
				scaleVec3(vOrientation, fWidth*0.5f, vToAdd);
				addVec3(pUnit->vPos, vToAdd, pTriStrip->verts[uiPointIndex*2].point);
				subVec3(pUnit->vPos, vToAdd, pTriStrip->verts[uiPointIndex*2+1].point);
				vec3RunningMin(pTriStrip->verts[uiPointIndex*2].point, vWorldMin);
				vec3RunningMax(pTriStrip->verts[uiPointIndex*2].point, vWorldMax);
				copyVec4(vColor, pTriStrip->verts[uiPointIndex*2].color);
				copyVec4(vColor, pTriStrip->verts[uiPointIndex*2+1].color);
				setVec2(pTriStrip->verts[uiPointIndex*2].texcoord, 1, fTexCoord);
				setVec2(pTriStrip->verts[uiPointIndex*2+1].texcoord, 0, fTexCoord);
				pTriStrip->verts[uiPointIndex*2].tightenup = pMeshParticle->pDraw->fTightenUp;
				pTriStrip->verts[uiPointIndex*2+1].tightenup = pMeshParticle->pDraw->fTightenUp;

				copyVec3(vNormal, pTriStrip->verts[uiPointIndex*2].normal);
				copyVec3(vNormal, pTriStrip->verts[uiPointIndex*2+1].normal);
				copyVec3(vBinormal, pTriStrip->verts[uiPointIndex*2].binormal);
				copyVec3(vBinormal, pTriStrip->verts[uiPointIndex*2+1].binormal);
				copyVec3(vTangent, pTriStrip->verts[uiPointIndex*2].tangent);
				copyVec3(vTangent, pTriStrip->verts[uiPointIndex*2+1].tangent);

				++uiPointIndex;
			}
			else // it's a cylinder, so generate a ring of verts for each unit, and generate indices as we go
			{
				U32 uiSegmentIndex;
				assert(uiPointIndex*3 < pCylTrail->num_constants);
				// Vertex constants:
				// Forward Vec3(3), texcoord
				// Position(3), radius(1)
				// Color(4)
				orientYPR(pCylTrail->vertex_shader_constants[uiPointIndex*3 + 0], vUnitForward);
				pCylTrail->vertex_shader_constants[uiPointIndex*3 +0][3] = calculateTrailTexCoord(pMeshTrail, pUnit, uiPointIndex, uiNumPoints);
				copyVec3(pUnit->vPos, pCylTrail->vertex_shader_constants[uiPointIndex*3 + 1]);
				vec3RunningMinMax(pUnit->vPos, vWorldMin, vWorldMax);
				pCylTrail->vertex_shader_constants[uiPointIndex*3 + 1][3] = fWidth * 0.5f; // width is diameter, gfxlib wants radius
				copyVec4(vColor, pCylTrail->vertex_shader_constants[uiPointIndex*3 + 2]);

				for (uiSegmentIndex=0; uiSegmentIndex<uiNumCylinderVertsPerSegment; ++uiSegmentIndex)
				{
					U32 uiVertIndex = uiPointIndex*uiNumCylinderVertsPerSegment + uiSegmentIndex;
					F32 fAngle = (uiSegmentIndex * TWOPI) / uiNumCylinderSegments;
					/*
					F32 fSinAngle, fCosAngle;
					sincosf(fAngle, &fSinAngle, &fCosAngle);
					pCylTrail->verts[uiVertIndex].point[0] = fCosAngle;
					pCylTrail->verts[uiVertIndex].point[1] = fSinAngle;
					pCylTrail->verts[uiVertIndex].point[2] = fAngle;
					*/

					assert(uiVertIndex < pCylTrail->vert_count);
					pCylTrail->verts[uiVertIndex].angle = fAngle;
					pCylTrail->verts[uiVertIndex].boneidx[0] = (int)uiPointIndex;
                    pCylTrail->verts[uiVertIndex].boneidx[1] = 0;
                    //pCylTrail->verts[uiVertIndex].boneidx[2] = 0;
                    //pCylTrail->verts[uiVertIndex].boneidx[3] = 0;

					// xform vNewVert
					/*
					mulVecMat3(vNewVert, mRot, pCylTrail->verts[uiVertIndex].normal);
					scaleVec3(pCylTrail->verts[uiVertIndex].normal, fWidth * 0.5f, pCylTrail->verts[uiVertIndex].point);
					addVec3(pCylTrail->verts[uiVertIndex].point, pUnit->vPos, pCylTrail->verts[uiVertIndex].point);
					copyVec4(vColor, pCylTrail->verts[uiVertIndex].color);
					pCylTrail->verts[uiVertIndex].texcoord[0] = ((F32)uiSegmentIndex / (uiNumCylinderSegments-1));
					pCylTrail->verts[uiVertIndex].texcoord[1] = (F32)uiPointIndex / uiNumPoints;
					pCylTrail->verts[uiVertIndex].texcoord[0] = pCylTrail->verts[uiVertIndex].texcoord[1] = 0.0f;
					*/
				}
				// Generate vert indices
				if ( uiPointIndex > 0 )
				{
					U32 uiStartingIndexIndex = 6 * uiNumCylinderSegments * (uiPointIndex - 1);
					for (uiSegmentIndex=0; uiSegmentIndex<uiNumCylinderSegments; ++uiSegmentIndex)
					{
						// Add face indices for this segment
						U32 uiFaceIndexIndex = uiStartingIndexIndex + uiSegmentIndex * 6;
						U32 uiFaceVerts[4];
						uiFaceVerts[0] = (uiPointIndex-1)*uiNumCylinderVertsPerSegment + uiSegmentIndex;
						uiFaceVerts[1] = (uiPointIndex-1)*uiNumCylinderVertsPerSegment + (uiSegmentIndex+1);
						uiFaceVerts[2] = uiPointIndex*uiNumCylinderVertsPerSegment + uiSegmentIndex;
						uiFaceVerts[3] = uiPointIndex*uiNumCylinderVertsPerSegment + (uiSegmentIndex+1);
						assert(uiFaceIndexIndex + 5 < pCylTrail->index_count);
						pCylTrail->idxs[uiFaceIndexIndex + 0] = uiFaceVerts[0];
						pCylTrail->idxs[uiFaceIndexIndex + 1] = uiFaceVerts[1];
						pCylTrail->idxs[uiFaceIndexIndex + 2] = uiFaceVerts[2];
						pCylTrail->idxs[uiFaceIndexIndex + 3] = uiFaceVerts[1];
						pCylTrail->idxs[uiFaceIndexIndex + 4] = uiFaceVerts[3];
						pCylTrail->idxs[uiFaceIndexIndex + 5] = uiFaceVerts[2];
					}
				}
				++uiPointIndex;
			}
		}

		// Advance the list
		pPrevUnit = pUnit;
		pUnit = pNextUnit;
	}

	dynNodeGetWorldSpacePos(&pDraw->node, vPartPos);

	{
		Vec4_aligned eye_bounds[8];
		mulBounds(vWorldMin, vWorldMax, gdraw->visual_pass->viewmat, eye_bounds);
		gfxGetScreenSpace(gdraw->visual_pass->frustum, gdraw->nextVisProjection, 1, eye_bounds, &instance_params.screen_area);
	}






	// Prepare instance params and material
	{
		gfxPrepareDynParticleMNCs(pMeshParticle->peaMNCRename, peaMNC, pMeshParticle->bMultiColor, pDraw);

		instance_params.wireframe = dynDebugState.bFxNotWireframe?0:gdraw->global_wireframe;
		setVec3same(instance_params.ambient_multiplier, 1);

		instance_params.uses_far_depth_range = gdraw->use_far_depth_range;
		if (pMeshParticle->bLowRes)
			instance_params.add_material_flags |= RMATERIAL_LOW_RES_ALPHA;

		instance_params.per_drawable_data = &per_drawable_data;

		{
			Vec3 vWorldMid;
			addVec3(vWorldMin, vWorldMax, vWorldMid);
			scaleVec3(vWorldMid, 0.5f, vWorldMid);
			addVec3(vWorldMid, gdraw->pos_offset, vWorldMid);
			instance_params.zdist = rdrDrawListCalcZDist(draw_list, vWorldMid) - pDraw->fTightenUp;
		}

		RDRALLOC_SUBOBJECT_PTRS(instance_params, 1);
		instance_params.subobjects[0] = rdrDrawListAllocSubobject(draw_list, 2);
		gfxDemandLoadMaterialAtQueueTime(instance_params.subobjects[0], pDraw->pMaterial, NULL, peaMNC?(*peaMNC):NULL, NULL, NULL, per_drawable_data.instance_param, instance_params.zdist, fUvDensity);

	}


	if ( bCylinder )
	{
		// Ok, generate vertex indices, which are always the same for a given number of rings and segments per ring
		if (pMeshParticle->bLowRes)
			pCylTrail->add_material_flags |= RMATERIAL_LOW_RES_ALPHA;
		rdrDrawListAddCylinderTrail(draw_list, pCylTrail, &instance_params, vPartPos, RST_ALPHA, draw_params->wl_params.is_costume?ROC_CHARACTER:ROC_FX);
	}
	else
	{
		// Only draw the ones we need to
		if (uiPointIndex < 2)
			return;

		pTriStrip->vert_count = uiPointIndex * 2;
		if (pMeshParticle->bLowRes)
			pTriStrip->add_material_flags |= RMATERIAL_LOW_RES_ALPHA;
		rdrDrawListAddTriStrip(draw_list, pTriStrip, &instance_params, vPartPos, RST_ALPHA, draw_params->wl_params.is_costume?ROC_CHARACTER:ROC_FX);
	}
}

__forceinline static void fillLightInfo(
	RdrAddInstanceParams *params, const DynDrawParams *draw_params,
	GfxDynObjLightCache *light_cache, RdrLightParams *dest_light_params)
{
	copyVec3(draw_params->wl_params.ambient_multiplier, params->ambient_multiplier);
	copyVec3(draw_params->wl_params.ambient_offset, params->ambient_offset);
	if (light_cache) {

		if(dest_light_params) {

			// Copy over light params.
			*dest_light_params = *gfxDynLightCacheGetLights(light_cache);
			params->light_params = dest_light_params;

			{
				Vec3 min = {
					draw_params->lights_root[0] - draw_params->lights_radius,
					draw_params->lights_root[1] - draw_params->lights_radius,
					draw_params->lights_root[2] - draw_params->lights_radius
				};

				Vec3 max = {
					draw_params->lights_root[0] + draw_params->lights_radius,
					draw_params->lights_root[1] + draw_params->lights_radius,
					draw_params->lights_root[2] + draw_params->lights_radius
				};

				gfxRemoveUnusedPointLightsFromLightParams(
					unitmat, min, max,
					dest_light_params);
			}

		} else {

			params->light_params = gfxDynLightCacheGetLights(light_cache);

		}
	}
}

static BasicTexture* pDefaultTexture1 = NULL;
static BasicTexture* pDefaultTexture2 = NULL;

static void gfxQueueGeometryParticle(DynParticle* pParticle, const DynDrawParams *draw_params_in)
{
	static BasicTexture** eaTexSwaps = NULL;
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	DynDrawParams draw_params = *draw_params_in;
	int i, j;
	RdrAddInstanceParams instance_params={0};
	RdrLightParams light_params;
	Vec3 world_min, world_max;
	ModelToDraw models[NUM_MODELTODRAWS];
	int model_count;
	DynDrawParticle* pDraw = pParticle->pDraw;
	Model *pModel = pDraw->pModel;
	Vec3 vScale;
	SkinningMat4 *bone_infos = NULL;
	MaterialNamedConstant ***peaMNC = &geometryNamedConstants;
	int num_bones = 0;
	F32 fMaxScale;
	WorldDrawableList *pDrawableList = NULL;
	WorldInstanceParamList *pInstanceParamList = NULL;
	F32 zdist, fTexDist, fUvDensity;
	bool effectivelyInstanceable;

	PERFINFO_AUTO_START_FUNC_L2();

	if (draw_params.bIgnoreNode)
	{
		copyMat4(unitmat, instance_params.instance.world_matrix);
		setVec3same(vScale, 1.0f);
		fMaxScale = 1.0f;
	}
	else
	{
		Quat qRot;
		dynNodeGetWorldSpaceRot(&pDraw->node, qRot);
		dynNodeGetWorldSpaceScale(&pDraw->node, vScale);
		dynNodeGetWorldSpacePos(&pDraw->node, instance_params.instance.world_matrix[3]);

		quatToMat(qRot, instance_params.instance.world_matrix);
		scaleMat3Vec3(instance_params.instance.world_matrix, vScale);

		fMaxScale = MAX(vScale[0], vScale[1]);
		MAX1(fMaxScale, vScale[2]);
	}

	if(vScale[0] == 0.0 || vScale[1] == 0.0 || vScale[2] == 0.0) {
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);

	// Apply a simple world-space offset.
	addVec3(
		instance_params.instance.world_matrix[3],
		pDraw->vDrawOffset,
		instance_params.instance.world_matrix[3]);

	mulVecMat4(pModel->mid, instance_params.instance.world_matrix, instance_params.instance.world_mid);
	zdist = rdrDrawListCalcZDist(gdraw->draw_list, instance_params.instance.world_mid);

	if (draw_params.wl_params.pDrawableList && draw_params.wl_params.iDrawableResetCounter == worldGetResetCount(true)) // protect against dereferencing freed drawables
	{
		// world debris
		pDrawableList = draw_params.wl_params.pDrawableList;
		pInstanceParamList = draw_params.wl_params.pInstanceParamList;
	}
	else if (pDraw->pDrawableList)
	{
		// regular debris or FX
		pDrawableList = pDraw->pDrawableList;
		pInstanceParamList = pDraw->pInstanceParamList;
	}

	if (pDrawableList)
		model_count = gfxDemandLoadPreSwappedModel(pDrawableList, models, ARRAY_SIZE(models), distance3(instance_params.instance.world_mid, gdraw->cam_pos), gdraw->lod_scale, -1, &pDraw->modelTracker, pModel->radius * fMaxScale, false);
	else
		model_count = gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), distance3(instance_params.instance.world_mid, gdraw->cam_pos), gdraw->lod_scale, -1, &pDraw->modelTracker, pModel->radius * fMaxScale);
	if (!model_count)
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}

	scaleAddVec3(onevec3, -fMaxScale * pModel->radius, instance_params.instance.world_mid, world_min);
	scaleAddVec3(onevec3, fMaxScale * pModel->radius, instance_params.instance.world_mid, world_max);
	for (i = 0; i < gdraw->pass_count; ++i)
	{
		if (draw_params.frustum_visible & gdraw->passes[i]->frustum_set_bit)
			frustumUpdateBounds(gdraw->passes[i]->frustum, world_min, world_max);
	}

	if (pParticle->bMultiColor)
	{
		if (pParticle->peaMNCRename)
		{
			// Replace geometryNamedConstants with our own version
			gfxResetEditableGeometryMNCs();
			FOR_EACH_IN_EARRAY((*pParticle->peaMNCRename), DynMNCRename, pMNCRename)
				FOR_EACH_IN_EARRAY(editableGeometryNamedConstants, MaterialNamedConstant, pMNC)
					if (pMNCRename->pcBefore == pMNC->name)
						pMNC->name = pMNCRename->pcAfter;
				FOR_EACH_END
			FOR_EACH_END
			peaMNC = &editableGeometryNamedConstants;
		}

		if (pDraw->fHueShift || pDraw->fSaturationShift || pDraw->fValueShift)
		{
			hsvShiftRGB(pDraw->vColor1, (*peaMNC)[0]->value, pDraw->fHueShift, pDraw->fSaturationShift, pDraw->fValueShift);
			hsvShiftRGB(pDraw->vColor2, (*peaMNC)[1]->value, pDraw->fHueShift, pDraw->fSaturationShift, pDraw->fValueShift);
			hsvShiftRGB(pDraw->vColor3, (*peaMNC)[2]->value, pDraw->fHueShift, pDraw->fSaturationShift, pDraw->fValueShift);
			(*peaMNC)[0]->value[3] = pDraw->vColor1[3];
			(*peaMNC)[1]->value[3] = pDraw->vColor2[3];
			(*peaMNC)[2]->value[3] = pDraw->vColor3[3];
			scaleVec4((*peaMNC)[0]->value, U8TOF32_COLOR, (*peaMNC)[0]->value);
			scaleVec4((*peaMNC)[1]->value, U8TOF32_COLOR, (*peaMNC)[1]->value);
			scaleVec4((*peaMNC)[2]->value, U8TOF32_COLOR, (*peaMNC)[2]->value);
		}
		else
		{
			scaleVec4(pDraw->vColor1, U8TOF32_COLOR, (*peaMNC)[0]->value);
			scaleVec4(pDraw->vColor2, U8TOF32_COLOR, (*peaMNC)[1]->value);
			scaleVec4(pDraw->vColor3, U8TOF32_COLOR, (*peaMNC)[2]->value);
		}
	}
	else
	{
		peaMNC = NULL;
	}

	if (eaSize(&pDraw->eaSkinChildren) > 0 && pDraw->pBaseSkeleton)
	{
		// Build skin matrices here
		int iBoneIndex;
		num_bones = eaSize(&pDraw->eaSkinChildren);
		bone_infos = _alloca(sizeof(SkinningMat4) * num_bones);
		for (iBoneIndex=0; iBoneIndex<num_bones; ++iBoneIndex)
		{
			const DynNode* pNode = pDraw->eaSkinChildren[iBoneIndex];
			const DynNode* pBaseNode = dynBaseSkeletonFindNode(pDraw->pBaseSkeleton, pNode->pcTag);
			Vec3 vBaseOffset;
			dynNodeGetWorldSpacePos(pBaseNode, vBaseOffset);
			scaleVec3(vBaseOffset, -1.0f, vBaseOffset);
			dynNodeCreateSkinningMatSlow(bone_infos[iBoneIndex], pNode, vBaseOffset, NULL);
		}
		copyMat4(unitmat, instance_params.instance.world_matrix);
		addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);
	}

	// If there are any models in this list that use skinning data, we
	// CANNOT use instancing with them!
	effectivelyInstanceable = pDraw->bExplicitlyInstanceable || draw_params.wl_params.is_debris;
	for(i = 0; effectivelyInstanceable && i < model_count; i++) {
		if(models[i].model->model_parent->header && eaSize(&models[i].model->model_parent->header->bone_names) > 0) {
			effectivelyInstanceable = false;
		}
	}

	// Pre-swapping is limited to debris and stuff that has it
	// explicitly enabled because we don't (yet) have a way to easily
	// determine if some parameters are animated.
	if (!draw_params.wl_params.pDrawableList && !pDraw->pDrawableList &&
		effectivelyInstanceable && !num_bones &&
		!draw_params.noMaterialPreSwap && !gfx_state.debug.dynDrawNoPreSwappedMaterials)
	{
		static const char **eaTexSwapStrings = NULL, **eaMaterialSwapStrings = NULL;;

		if (pDraw->pTexture || pDraw->pTexture2)
		{
			if (pDraw->pTexture)
			{
				eaPush(&eaTexSwapStrings, "FXTexture1");
				eaPush(&eaTexSwapStrings, pDraw->pTexture->name);
			}

			if (pDraw->pTexture2)
			{
				eaPush(&eaTexSwapStrings, "FXTexture2");
				eaPush(&eaTexSwapStrings, pDraw->pTexture2->name);
			}
		}

		assert(!pDraw->pInstanceParamList);

		pDrawableList = worldCreateDrawableList(pModel, NULL, NULL, NULL, 
			pDraw->pMaterial ? pDraw->pMaterial->material_name : NULL,
			&eaTexSwapStrings,
			&eaMaterialSwapStrings,
			NULL,
			peaMNC ? *peaMNC : NULL,
			&fx_drawable_pool, 
			DONT_WAIT_FOR_LOAD,
			WL_FOR_ENTITY,
			&pDraw->pInstanceParamList);

		if (pDrawableList)
		{
			assert(pDraw->pInstanceParamList);
			pDraw->pDrawableList = pDrawableList;
			pInstanceParamList = pDraw->pInstanceParamList;
		} else {
			assert(!pDraw->pInstanceParamList); // Otherwise need to free it?
		}

		eaClear(&eaTexSwapStrings);
	}

	{
		Vec3 vShiftedColor;

		hsvShiftRGB(
			pDraw->vColor,
			vShiftedColor,
			pDraw->vHSVShift[0], 
			pDraw->vHSVShift[1],
			pDraw->vHSVShift[2]);

		// Shared data (not per-instance)
		if (pDraw->fHueShift || pDraw->fSaturationShift || pDraw->fValueShift)
		{
			Vec3 vColor;
			hsvShiftRGB(vShiftedColor, vColor, pDraw->fHueShift, pDraw->fSaturationShift, pDraw->fValueShift);
			//hueShiftRGB(pDraw->vColor, vColor, pDraw->fHueShift);
			scaleVec3(vColor, U8TOF32_COLOR, instance_params.instance.color);
		}
		else
		{
			scaleVec3(vShiftedColor, U8TOF32_COLOR, instance_params.instance.color);
		}
	}

	if (pDraw->pTexture || pDraw->pTexture2)
	{
		if (!pDefaultTexture1)
		{
			pDefaultTexture1 = texLoadBasic(allocAddString("FXTexture1"), TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);
			pDefaultTexture2 = texLoadBasic(allocAddString("FXTexture2"), TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);
		}

		if (pDraw->pTexture)
		{
			eaPush(&eaTexSwaps, pDefaultTexture1);
			eaPush(&eaTexSwaps, pDraw->pTexture);
		}

		if (pDraw->pTexture2)
		{
			eaPush(&eaTexSwaps, pDefaultTexture2);
			eaPush(&eaTexSwaps, pDraw->pTexture2);
		}
	}

	if(pDraw->eaExtraTextureSwaps) {
		int ts;
		for(ts = 0; ts < eaSize(&pDraw->eaExtraTextureSwaps); ts++) {
			BasicTexture *pTex = texLoadBasic(pDraw->eaExtraTextureSwaps[ts], TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);
			eaPush(&eaTexSwaps, pTex);
		}
	}

	instance_params.distance_offset = pModel->radius;
	instance_params.wireframe = gfx_state.wireframe & 3;
	setVec3same(instance_params.ambient_multiplier, 1);
	instance_params.frustum_visible = draw_params.frustum_visible;
	if (draw_params.frustum_visible & gdraw->visual_frustum_bit)
	{
		Vec4_aligned eye_bounds[8];
		mulBounds(world_min, world_max, gdraw->visual_pass->viewmat, eye_bounds);
		gfxGetScreenSpace(gdraw->visual_pass->frustum, gdraw->nextVisProjection, 1, eye_bounds, &instance_params.screen_area);
		gfxGetObjectLightsUncached(
			&light_params,
			draw_params.region_graphics_data ? draw_params.region_graphics_data->region : NULL,
			instance_params.instance.world_mid, pModel->radius, true);
		instance_params.light_params = &light_params;

		/*
		if (instance_params.screen_area > 0.25f)
		{
			fNearPlaneFadeOut = calcInterpParam(instance_params.screen_area, 0.3333f, 0.25f);
			fNearPlaneFadeOut = CLAMP(fNearPlaneFadeOut, 0.3333f, 1.0f);
		}
		*/
	}

	fTexDist = zdist - pModel->radius * fMaxScale;

	// Draw each LOD model
	for (j=0; j<model_count; j++) 
	{
		WorldDrawableLod *lod = models[j].draw;
		RdrDrawableGeo *geo_draw = lod ? lod->subobjects[0]->model->temp_geo_draw : NULL;
		RdrInstancePerDrawableData per_drawable_data;
		bool bInvalidPreswap;
		int num_vs_consts = 0;

		if (!models[j].geo_handle_primary) {
			assert(0);
			continue;
		}

		instance_params.instance.color[3] = models[j].alpha * (pDraw->vColor[3] * U8TOF32_COLOR) * pParticle->fFadeOut;// * fNearPlaneFadeOut;

		//allocate space for the various splat-related stuff if required
		if (draw_params.additional_material_flags & RMATERIAL_ALPHA_FADE_PLANE) num_vs_consts += 1;
		if (draw_params.additional_material_flags & RMATERIAL_VS_TEXCOORD_SPLAT) num_vs_consts += 4;

		if (num_bones)
		{
			RdrDrawableSkinnedModel *skin_draw = rdrDrawListAllocSkinnedModel(gdraw->draw_list, RTYPE_SKINNED_MODEL, models[j].model, pDraw->pMaterial?1:models[j].model->geo_render_info->subobject_count, num_vs_consts, num_bones, NULL);
			// TODO DJR Uncomment and fix this below, FX skinned geometry has to stop the memcpy madness too
			if (skin_draw)
			{
				memcpy(skin_draw->skinning_mat_array, bone_infos, sizeof(SkinningMat4) * skin_draw->num_bones);
				geo_draw = &skin_draw->base_geo_drawable;
			}
			if (!geo_draw)
				continue;

			geo_draw->geo_handle_primary = models[j].geo_handle_primary;

			dynDebugState.frameCounters.uiNumDrawnSkinnedGeoParticles += geo_draw->subobject_count;
			if (draw_params.wl_params.is_costume)
				dynDebugState.frameCounters.uiNumDrawnCostumeFx += geo_draw->subobject_count;
			else if (draw_params.wl_params.is_debris)
				dynDebugState.frameCounters.uiNumDrawnDebris += geo_draw->subobject_count;
			else
			{
				dynDebugState.frameCounters.uiNumDrawnFx += geo_draw->subobject_count;

				if (dynDebugState.bFxDebugOn)
				{
					DynDrawTracker* pDrawTracker = dynDrawTrackerPush();
					DynFxInfo* pInfo = draw_params.wl_params.pInfo;
					pDrawTracker->pcName = pInfo->pcDynName;
					pDrawTracker->ePriority = draw_params.wl_params.iPriorityLevel;
					pDrawTracker->eType = eDynFxType_SkinnedGeometry;
					pDrawTracker->uiSubObjects = geo_draw->subobject_count;
					pDrawTracker->uiNum = 1;
					if (dynDebugState.bFxDrawOnlySelected)
					{
						bool bDraw = false;
						FOR_EACH_IN_EARRAY(eaDynDrawOnly, const char, pcDrawOnly)
						{
							if (pcDrawOnly == pInfo->pcDynName)
							{
								bDraw = true;
								break;
							}
						}
						FOR_EACH_END;

						if (!bDraw)
							continue;
					}
				}
			}
		}
		else if (!geo_draw)
		{
			geo_draw = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[j].model, models[j].model->geo_render_info->subobject_count, num_vs_consts, 0);
			if (!geo_draw)
				continue;

			geo_draw->geo_handle_primary = models[j].geo_handle_primary;

			if (lod)
				lod->subobjects[0]->model->temp_geo_draw = geo_draw;

			if (draw_params.wl_params.is_costume)
				dynDebugState.frameCounters.uiNumDrawnCostumeFx += geo_draw->subobject_count;
			else if (draw_params.wl_params.is_debris)
				dynDebugState.frameCounters.uiNumDrawnDebris += geo_draw->subobject_count;
			else
			{
				dynDebugState.frameCounters.uiNumDrawnFx += geo_draw->subobject_count;
				if (dynDebugState.bFxDebugOn)
				{
					DynDrawTracker* pDrawTracker = dynDrawTrackerPush();
					DynFxInfo* pFxInfo = draw_params.wl_params.pInfo;
					pDrawTracker->pcName = pFxInfo->pcDynName;
					pDrawTracker->ePriority = draw_params.wl_params.iPriorityLevel;
					pDrawTracker->eType = eDynFxType_Geometry;
					pDrawTracker->uiSubObjects = geo_draw->subobject_count;
					pDrawTracker->uiNum = 1;
					if (dynDebugState.bFxDrawOnlySelected)
					{
						bool bDraw = false;
						FOR_EACH_IN_EARRAY(eaDynDrawOnly, const char, pcDrawOnly)
						{
							if (pcDrawOnly == pFxInfo->pcDynName)
							{
								bDraw = true;
								break;
							}
						}
						FOR_EACH_END;

						if (!bDraw)
							continue;
					}
				}
			}
			dynDebugState.frameCounters.uiNumDrawnGeoParticles += geo_draw->subobject_count;
		}
		else
		{
			dynDebugState.frameCounters.uiNumInstancedGeoParticles += geo_draw->subobject_count;
		}

		bInvalidPreswap = !lod || lod->subobject_count != geo_draw->subobject_count;

		instance_params.instance.morph = models[j].morph;

		instance_params.needs_late_alpha_pass_if_need_grab = !rdr_state.disableTwoPassRefraction; // Use late pass unless we disable 2 pass refraction

		if (draw_params.additional_material_flags & RMATERIAL_VS_TEXCOORD_SPLAT) {
			fUvDensity = TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY;
		} else {
            fUvDensity = models[j].model->uv_density - log2f(fabsf(fMaxScale));
		}

		{
			int iCurrentMaterial;
			
			// We need to draw the materials in this order...
			//   Dissolve material (if it exists)
			//   Normal material
			//   Material adds (if they exist)

			// Total number of materials is the normal material + the
			// dissolve material (if it exists) + extra material adds.
			for(iCurrentMaterial = 0; iCurrentMaterial < 1 + !!pDraw->pGeoDissolveMaterial + eaSize(&pDraw->eaGeoAddMaterials); iCurrentMaterial++) {

				instance_params.add_material_flags = draw_params.additional_material_flags;

				if(pDraw->pGeoDissolveMaterial && (iCurrentMaterial == 0)) {
					
					// Dissolve material.

					SETUP_INSTANCE_PARAMS;
					RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[j].model, i);

					for (i = 0; i < geo_draw->subobject_count; i++)
						gfxDemandLoadMaterialAtQueueTime(
							instance_params.subobjects[i],
							pDraw->pGeoDissolveMaterial,
							eaSize(&eaTexSwaps)>0?eaTexSwaps:NULL,
							peaMNC?*peaMNC:NULL, NULL, NULL,
							instance_params.per_drawable_data[i].instance_param,
							fTexDist, fUvDensity);


				} else if(iCurrentMaterial > !!pDraw->pGeoDissolveMaterial) {

					// Extra material adds start after 0 if there is no
					// dissolve material, or 1 if there is a dissolve
					// material.

					int iMatAddIndex = iCurrentMaterial - (1 + !!pDraw->pGeoDissolveMaterial);

					SETUP_INSTANCE_PARAMS;
					RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[j].model, i);

					// Only draw the added material where we already drew a regular one.

					instance_params.add_material_flags |= RMATERIAL_DEPTH_EQUALS|RMATERIAL_NOZWRITE;

					for (i = 0; i < geo_draw->subobject_count; i++) {
						gfxDemandLoadMaterialAtQueueTime(
							instance_params.subobjects[i],
							pDraw->eaGeoAddMaterials[iMatAddIndex],
							eaSize(&eaTexSwaps)>0?eaTexSwaps:NULL,
							peaMNC?*peaMNC:NULL, NULL, NULL,
							instance_params.per_drawable_data[i].instance_param,
							fTexDist, fUvDensity);
					}

				} else if (!num_bones && !bInvalidPreswap) {

					// Do pre-swapping and instancing stuff.
					SETUP_INSTANCE_PARAMS;
					RDRALLOC_SUBOBJECT_PTRS(instance_params, geo_draw->subobject_count);
					for (i = 0; i < geo_draw->subobject_count; i++)
					{
						if (i < geo_draw->subobject_count - 1)
							PREFETCH(lod->subobjects[i + 1]);
						if (!lod->subobjects[i]->temp_rdr_subobject)
						{
							lod->subobjects[i]->temp_rdr_subobject = rdrDrawListAllocSubobject(gdraw->draw_list, models[j].model->data->tex_idx[i].count);
							lod->subobjects[i]->fallback_idx = getMaterialDrawIndex(lod->subobjects[i]->material_draws);
							lod->subobjects[i]->material_render_info = gfxDemandLoadPreSwappedMaterialAtQueueTime(lod->subobjects[i]->temp_rdr_subobject, lod->subobjects[i]->material_draws[lod->subobjects[i]->fallback_idx], fTexDist, fUvDensity);
						}
						else
						{
							MIN1F(lod->subobjects[i]->material_render_info->material_min_draw_dist[gfx_state.currentAction->action_type], fTexDist);
							MIN1F(lod->subobjects[i]->material_render_info->material_min_uv_density, fUvDensity);
						}
						instance_params.subobjects[i] = lod->subobjects[i]->temp_rdr_subobject;
						if (pInstanceParamList)
							copyVec4(pInstanceParamList->lod_params[models[j].lod_index].subobject_params[i].fallback_params[lod->subobjects[i]->fallback_idx].instance_param, instance_params.per_drawable_data[i].instance_param);
						else
							zeroVec4(instance_params.per_drawable_data[i].instance_param);
					}

				} else if (pDraw->pMaterial || pDraw->pMaterial2) {

					// Non-instanced, materials specified in the FX.

					SETUP_INSTANCE_PARAMS;
					RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[j].model, i);

					if(pDraw->pGeoDissolveMaterial && (iCurrentMaterial != 0)) {
						instance_params.add_material_flags |= RMATERIAL_DEPTH_EQUALS|RMATERIAL_NOZWRITE;
					}

					for (i = 0; i < geo_draw->subobject_count; i++) {
						
						Material *pMat = 						
							i == 1 ? pDraw->pMaterial2 : pDraw->pMaterial;

						// If no material is specified, fall back on
						// the model's default material.
						if(!pMat) {
							pMat = models[j].model->materials[i];
						}

						gfxDemandLoadMaterialAtQueueTime(
							instance_params.subobjects[i],
							pMat,
							eaSize(&eaTexSwaps)>0?eaTexSwaps:NULL,
							peaMNC?*peaMNC:NULL, NULL, NULL,
							instance_params.per_drawable_data[i].instance_param,
							fTexDist, fUvDensity);
					}
				}
				else
				{
					// Non-instanced, default materials from the model.
					
					SETUP_INSTANCE_PARAMS;
					RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[j].model, i);

					if(pDraw->pGeoDissolveMaterial && (iCurrentMaterial != 0)) {
						instance_params.add_material_flags |= RMATERIAL_DEPTH_EQUALS|RMATERIAL_NOZWRITE;
					}

					for (i = 0; i < geo_draw->subobject_count; i++)
						gfxDemandLoadMaterialAtQueueTime(
							instance_params.subobjects[i],
							models[j].model->materials[i],
							eaSize(&eaTexSwaps)>0?eaTexSwaps:NULL,
							peaMNC?*peaMNC:NULL, NULL, NULL,
							instance_params.per_drawable_data[i].instance_param,
							fTexDist, fUvDensity);
				}

				if (pParticle->bLowRes)
					instance_params.add_material_flags |= RMATERIAL_LOW_RES_ALPHA;

				if (draw_params.pSplatInfo && gfxSplatIsUsingShaderProjection(draw_params.pSplatInfo->pGfxSplat))
				{
					//we need to have this flag or else we are just clobbering stuff setting the vs constants
					assert(instance_params.add_material_flags & RMATERIAL_VS_TEXCOORD_SPLAT);

					if (instance_params.add_material_flags & RMATERIAL_ALPHA_FADE_PLANE)
					{
						makePlane2(draw_params.pSplatInfo->pGfxSplat->fade_plane_pt, draw_params.pSplatInfo->pGfxSplat->initial_start[2], geo_draw->vertex_shader_constants[0]);
						gfxSplatGetTextureMatrix(draw_params.pSplatInfo->pGfxSplat, &geo_draw->vertex_shader_constants[1]); //using 1-5 to store a 4x4 mat
					}
					else
					{
						gfxSplatGetTextureMatrix(draw_params.pSplatInfo->pGfxSplat, &geo_draw->vertex_shader_constants[0]); //using 0-4 to store a 4x4 mat
					}
				}

				if (num_bones)
					rdrDrawListAddSkinnedModel(gdraw->draw_list, (RdrDrawableSkinnedModel *)geo_draw, &instance_params, RST_AUTO, draw_params.wl_params.is_costume?ROC_CHARACTER:ROC_FX);
				else
					rdrDrawListAddGeoInstance(gdraw->draw_list, geo_draw, &instance_params, 
					 RST_AUTO, draw_params.wl_params.is_costume?ROC_CHARACTER:ROC_FX, true);

				if (instance_params.uniqueDrawCount)
					gfxGeoIncrementUsedCount(models[j].model->geo_render_info, instance_params.uniqueDrawCount, true);
			}
		}

	}

	eaClear(&eaTexSwaps);
	PERFINFO_AUTO_STOP_L2();
}

static void gfxQueueDynSplat(DynParticle* pParticle, const DynDrawParams *draw_params_in)
{
	DynSplat* pSplat = pParticle->pDraw->pSplat;
	DynDrawParticle* pDraw = pParticle->pDraw;
	Quat qRot;
	Vec3 vSplatDir;
	Mat4 mSplat;
	DynDrawParams draw_params = *draw_params_in;
	Vec3 vScale = {1, 1, 1};
	F32 fSplatRadiusScale = 1;
	Mat4 mTexProjection;
	DynNode *texProjectNode = GET_REF(pSplat->hSplatProjectionNode);

	if(pSplat->bUpdateScale) {
		dynNodeGetWorldSpaceScale(&pDraw->node, vScale);
		fSplatRadiusScale = (vScale[0] + vScale[1]) * 0.5;
	}
	
	if (pSplat->bForceDown)
	{
		setVec3(vSplatDir, 0.0f, -pSplat->fSplatLength, 0.0f);
		setVec3(mSplat[0], 1.0f, 0.0f, 0.0f);
		setVec3(mSplat[1], 0.0f, 0.0f, 1.0f);
		setVec3(mSplat[2], 0.0f, -1.0f, 0.0f);
		dynNodeGetWorldSpacePos(&pDraw->node, mSplat[3]);
	}
	else
	{
		dynNodeGetWorldSpaceMat(&pDraw->node, mSplat, false);
		dynNodeGetWorldSpaceRot(&pDraw->node, qRot);
		quatRotateVec3Inline(qRot, forwardvec, vSplatDir);
		scaleVec3(vSplatDir, pSplat->fSplatLength * vScale[2], vSplatDir);
	}

	if (pSplat->bCenterLength)
		scaleAddVec3(vSplatDir, -0.5f, mSplat[3], mSplat[3]);

	if(texProjectNode) {
		dynNodeGetWorldSpaceMat(texProjectNode, mTexProjection, false);
	} else {
		copyMat4(mSplat, mTexProjection);
	}

	if (!pSplat->pGfxSplat)
	{
		int splatFlags = GFX_SPLAT_UNIT_TEXCOORD | GFX_SPLAT_NO_TANGENTSPACE;

		if(pSplat->bDisableCulling) {
			splatFlags |= GFX_SPLAT_TWO_SIDED;
		} else {
			splatFlags |= GFX_SPLAT_CULL_UNIT_TEX;
		}

		pSplat->pGfxSplat = gfxCreateSplatMtx(
			mSplat, mTexProjection, ( ( pSplat->eType == eDynSplatType_Cone ) ? pSplat->fSplatInnerRadius : pSplat->fSplatRadius ) * fSplatRadiusScale, 
			1.0f, vSplatDir, pSplat->fSplatRadius * fSplatRadiusScale,
			splatFlags, pParticle->pDraw->pMaterial, 6.0f,
			1.0 - pSplat->fSplatFadePlanePt);

		pParticle->pDraw->pModel = gfxSplatGetModel(pSplat->pGfxSplat, 0);
	}
	else
	{
		gfxUpdateSplatMtx(
			mSplat, mTexProjection,
			( ( pSplat->eType == eDynSplatType_Cone ) ? pSplat->fSplatInnerRadius : pSplat->fSplatRadius ) * fSplatRadiusScale,
			1.0f, vSplatDir, pSplat->fSplatRadius * fSplatRadiusScale,
			pSplat->pGfxSplat,
			draw_params_in->wl_params.splats_invalid, 1.0 - pSplat->fSplatFadePlanePt);
		if (dynDebugState.bDrawSplatCollision)
		{
			Vec3 vSplatEndPos;
			F32 splatLen = lengthVec3(vSplatDir);
			Vec3 vNormalizedDir;
			Vec3 vStartPlusTolerance;
			Vec3 vEndPlusTolerance;
			F32 fSqrtTolerance = fsqrt(pSplat->pGfxSplat->tolerance);
			F32 fExtendLength = SPLAT_ENDCAP_TOLERANCE_RADIUS_SCALE * pSplat->pGfxSplat->initial_radius;

			// Draw the current splat cylinder.
			addVec3(vSplatDir, mSplat[3], vSplatEndPos);
			gfxDrawCylinder3D(mSplat[3], vSplatEndPos, pSplat->fSplatRadius * fSplatRadiusScale, 12, 1, ARGBToColor(0xFF00FFFF), 1.0f);

			// Draw a splat cylinder indicating how we actually did the triangle query
			// last time the splat was recalculated.
			addVec3(pSplat->pGfxSplat->initial_direction, pSplat->pGfxSplat->initial_start[3], vSplatEndPos);
			copyVec3(pSplat->pGfxSplat->initial_direction, vNormalizedDir);
			normalVec3(vNormalizedDir);
			scaleAddVec3(vNormalizedDir, -fExtendLength, pSplat->pGfxSplat->initial_start[3], vStartPlusTolerance);
			scaleAddVec3(vNormalizedDir, fExtendLength, pSplat->pGfxSplat->initial_start[3], vEndPlusTolerance);
			addVec3(vEndPlusTolerance, pSplat->pGfxSplat->initial_direction, vEndPlusTolerance);

			gfxDrawCylinder3D(
				vStartPlusTolerance,
				vEndPlusTolerance,
				// MAXF(pSplat->pGfxSplat->initial_radius, splatLen) * fSplatRadiusScale + fSqrtTolerance,
				pSplat->pGfxSplat->initial_radius + fSqrtTolerance,
				12, 1, ARGBToColor(0xFF0000FF), 1.0f);
		}

	}

	draw_params.bIgnoreNode = true;
	draw_params.pSplatInfo = pSplat;
	draw_params.additional_material_flags |= RMATERIAL_DECAL | RMATERIAL_DEPTHBIAS | RMATERIAL_VS_TEXCOORD_SPLAT | RMATERIAL_ALPHA_FADE_PLANE;

	if (gfxSplatIsUsingShaderProjection(pSplat->pGfxSplat))
	{
		draw_params.additional_material_flags |= RMATERIAL_VS_TEXCOORD_SPLAT;
	}

	// Draw each model.
	{
		int i;
		for(i = 0; i < gfxSplatGetNumModels(pSplat->pGfxSplat); i++) {
			assert(pParticle);
			pParticle->pDraw->pModel = gfxSplatGetModel(pSplat->pGfxSplat, i);
			gfxQueueGeometryParticle(pParticle, &draw_params);
		}
	}
}

static void gfxQueueFastParticle(DynFxFastParticleSet* pSet, const DynDrawParams *draw_params)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	RdrDrawListPassData *pass = gdraw->visual_pass;


	if(pSet->bLightModulation) {
		gfxCalcSimpleLightValueForPoint(pSet->vPos, pSet->vModulateColor);
	}

	if (!pass || !(draw_params->frustum_visible & pass->frustum_set_bit))
		return;

	gfxQueueSingleFastParticleSet(pSet, draw_params, pSet->fSystemAlpha);

}

static void gfxQueueClothParticle(DynParticle* pParticle, const DynDrawParams *draw_params_in);

static void gfxQueueDynParticle(DynParticle *pParticle, const DynDrawParams *draw_params_in)
{
	DynDrawParticle* pDraw = pParticle->pDraw;
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	DynDrawParams draw_params = *draw_params_in;
	F32 zdist;
	F32 fUvDensity;
	Vec3 vNodePos;
	Vec3 world_mid;

	PERFINFO_AUTO_START_L2(__FUNCTION__,1);

	{
		// Calculate z distance for texture loading, work is likely duplicated in child functions
		Vec3 vScale;
		dynNodeGetWorldSpacePos(&pDraw->node, vNodePos);
		addVec3(vNodePos, gdraw->pos_offset, world_mid);
		zdist = rdrDrawListCalcZDist(gdraw->draw_list, world_mid);

		dynNodeGetWorldSpaceScale(&pDraw->node, vScale);
		fUvDensity = MAXF(fabsf(vScale[0]), MAXF(fabsf(vScale[1]), fabsf(vScale[2])));
		if (fUvDensity)
			fUvDensity = -log2f(fUvDensity);
		else
			fUvDensity = TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY;
	}

	if (pDraw->pcTextureName)
	{
		if (!pDraw->pTexture) {

			if(pDraw->pcTexWords) {
				
				// Load texWords version.
				TexWordParams *params = createTexWordParams();
				char *paramCopy = strdup(pDraw->pcTexWords);
				int length = (U32)strlen(paramCopy);
				int i;
				char *last_s = paramCopy;

				// Split pcTexWords into multiple parameters
				// using '\' as a delimiter.
				for (i = 0; i < length; i++) {
					if (paramCopy[i] == '\\') {
						paramCopy[i] = 0;

						eaPush(&params->parameters, last_s);
						
						if (i < length-1)
							last_s = &paramCopy[i+1];
						else
							last_s = NULL;
					}
				}

				if(last_s) {
					eaPush(&params->parameters, last_s);
				}

				pDraw->pTexture = texFindDynamic(pDraw->pcTextureName, params, WL_FOR_FX, NULL);

			} else {

				pDraw->pTexture = texLoadBasic(pDraw->pcTextureName, TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);
			}
		}

		if (pDraw->pTexture)
			pDraw->hTexHandle = texDemandLoadInline(pDraw->pTexture, zdist, fUvDensity, white_tex);
		else
			pDraw->hTexHandle = texDemandLoadFixed(white_tex);
	}

	if (pDraw->pcTextureName2)
	{
		if (!pDraw->pTexture2)
			pDraw->pTexture2 = texLoadBasic(pDraw->pcTextureName2, TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);

		if (pDraw->pTexture2)
			pDraw->hTexHandle2 = texDemandLoadInline(pDraw->pTexture2, zdist, fUvDensity, white_tex);
		else
			pDraw->hTexHandle2 = texDemandLoadFixed(white_tex);
	}

	if (pDraw->pcModelName)
	{
		if (!pDraw->pModel)
		{
			pDraw->pModel = modelFind(pDraw->pcModelName, true, WL_FOR_FX);
			if (!pDraw->pModel)
			{
				Errorf("Unable to find geometry %s for particle system", pDraw->pcModelName);
				pDraw->pModel = NULL;
				PERFINFO_AUTO_STOP_L2();
				return;
			}
		}
	}
	else if (!pDraw->pMaterial && pDraw->pTexture)
	{
		pDraw->pMaterial = materialFindNoDefault(
			pDraw->pcMaterialName ? pDraw->pcMaterialName : "Default_Particle", WL_FOR_FX);
		if (!pDraw->pMaterial)
		{
			pDraw->pMaterial = materialFindNoDefault("Default_Particle", WL_FOR_FX);
			if (!pDraw->pMaterial)
			{
				// Tried to load Default_Particle and failed
				FatalErrorf("Default_Particle.material not found, and is required!");
				PERFINFO_AUTO_STOP_L2();
				return;
			}
		}
	}

	draw_params.frustum_visible = gdraw->visual_frustum_bit;

	if (pDraw->bCastShadows && (pDraw->pModel || pDraw->pCloth))
	{
		Vec3 vEyePos;
		int clip, k;

		for (k = 0; k < gdraw->pass_count; ++k)
		{
			RdrDrawListPassData *pass = gdraw->passes[k];
			float radius;

			if(pDraw->pModel) {
				radius = pDraw->pModel->radius;
			} else if(pDraw->pCloth && pDraw->pCloth->pModel) {
				radius = pDraw->pCloth->pModel->radius;
			} else {
				radius = 1;
			}

			if (pass == gdraw->visual_pass)
				continue; // already set

			mulVecMat4(vNodePos, pass->viewmat, vEyePos);

			if (!(clip=frustumCheckSphere(pass->frustum, vEyePos, radius)))
				continue;

			draw_params.frustum_visible |= pass->frustum_set_bit;
		}
	}

	if (pDraw->pMeshTrail)
		gfxQueueMeshTrail(pParticle, &draw_params);
	else if (pDraw->pSplat)
		gfxQueueDynSplat(pParticle, &draw_params);
	else if (pDraw->pCloth)
		gfxQueueClothParticle(pParticle, &draw_params);
	else if (pDraw->pModel)
		gfxQueueGeometryParticle(pParticle, &draw_params);
	else if (pDraw->pDynFlare)
	{
#define MAX_LENS_FLARE_PIECES 16
		LensFlarePiece flare_pieces[ MAX_LENS_FLARE_PIECES ] = { 0 };
		LensFlarePiece *flare_pieces_array[ MAX_LENS_FLARE_PIECES ] = { 0 };
		DynFlare * pFlare = pDraw->pDynFlare;
		int num_flares = eafSize(&pFlare->size);

		if (num_flares)
		{
			int i;
			if (num_flares > MAX_LENS_FLARE_PIECES)
				num_flares = MAX_LENS_FLARE_PIECES;
			for (i = 0; i < num_flares; ++i)
			{
				flare_pieces_array[i] = flare_pieces + i;
				flare_pieces[i].size = pFlare->size[i];
				copyVec3(pFlare->hsv_color, flare_pieces[i].hsv_color);
				flare_pieces[i].position = pFlare->position[i];
				flare_pieces[i].material_name = (char*)pFlare->ppcMaterials[i];
				flare_pieces[i].texture_name = (char*)pDraw->pcTextureName;
				flare_pieces[i].material = materialFind(flare_pieces[i].material_name, WL_FOR_WORLD);
				flare_pieces[i].texture = pDraw->pTexture;
			}

			if(gdraw->num_object_lens_flares < 4) {
				addVec3(gdraw->pos_offset, vNodePos, gdraw->lens_flare_positions[gdraw->num_object_lens_flares]);
				gfxSkyDrawLensFlare(flare_pieces_array, num_flares, gdraw->visual_pass->frustum, vNodePos, unitvec3, 1.0f, gdraw->num_sky_lens_flares + gdraw->num_object_lens_flares);
				gdraw->num_object_lens_flares++;
			} else {
				// Draw this lens flare with no occlusion test, because we're over our limit. (5 is more than the number of slots in the z occlusion surface.)
				gfxSkyDrawLensFlare(flare_pieces_array, num_flares, gdraw->visual_pass->frustum, vNodePos, unitvec3, 1.0f, 5);
			}
		}
	}
	else if (pDraw->pTexture || (pDraw->pMaterial && pDraw->iEntMaterial == edemmNone))
		gfxQueueSpriteParticle(pParticle, &draw_params);
	if (pDraw->pDrawSkeleton) {
		gfxQueueSingleDynDrawSkeleton(pDraw->pDrawSkeleton, NULL, false, draw_params.bForceSkinnedShadows, NULL);
		FOR_EACH_IN_EARRAY(pDraw->pDrawSkeleton->eaSubDrawSkeletons, DynDrawSkeleton, pSub)
			gfxQueueSingleDynDrawSkeleton(pSub, NULL, false, draw_params.bForceSkinnedShadows, NULL);
		FOR_EACH_END;
	}

	pDraw->fTimeSinceLastDraw = 0;

	PERFINFO_AUTO_STOP_L2();
}

//////////////////////////////////////////////////////////////////////////
// skinning

static const char *s_pcColor0;
static const char *s_pcColor1;
static const char *s_pcColor2;
static const char *s_pcColor3;
static const char *s_pcTexXfrm;
AUTO_RUN;
void gfxModelDrawInitStrings(void)
{
	s_pcColor0 = allocAddStaticString("Color0");
	s_pcColor1 = allocAddStaticString("Color1");
	s_pcColor2 = allocAddStaticString("Color2");
	s_pcColor3 = allocAddStaticString("Color3");
	s_pcTexXfrm = allocAddStaticString("TexXfrm");
}

static bool gfxPrepareModelDrawParams(
	DynDrawParams* draw_params, RdrAddInstanceParams* instance_params,
	DynDrawModel* pGeo, DynDrawSkeleton* pDrawSkeleton,
	Vec3 color2, F32 *alpha,
	MaterialNamedConstant*** peaMNCCopy, MaterialNamedConstant*** peaMNCCopy2, WLCostume* pCostume,
	bool bPreSwappedMaterials,
	RdrLightParams *dest_light_params)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	int i = 0;
	F32 temp_alpha = *alpha;
	bool dont_use_costume_constants = draw_params->wl_params.current_material_swap && draw_params->wl_params.current_material_swap->dont_use_costume_constants;

	copyVec3(draw_params->wl_params.color, instance_params->instance.color);

	// Shared data (not per-instance)
	instance_params->frustum_visible = draw_params->frustum_visible;
	if (pGeo->bNoShadow)
	{
		int frustum_shadow_mask = 0;

		// Check each pass. If the pass is not a shadow pass, we can draw that pass (set the frustum bit)
		FOR_EACH_IN_EARRAY_FORWARDS(gdraw->passes, RdrDrawListPassData, pPass)
			if (!pPass->shadow_light)
				frustum_shadow_mask |= pPass->frustum_set_bit;
		FOR_EACH_END;

		instance_params->frustum_visible &= frustum_shadow_mask;

		// If no longer visible we can abort the draw call
		if (!instance_params->frustum_visible)
		{
			return false;
		}

	}
	instance_params->distance_offset = pGeo->pModel->radius + pGeo->fSortBias;
	instance_params->wireframe = gfx_state.wireframe & 3;
	setVec3same(instance_params->ambient_multiplier, 1);

	if (peaMNCCopy2 && color2 && pGeo->pMaterial[1])
	{
		copyVec3(draw_params->wl_params.color, color2);
	}

	if (bPreSwappedMaterials)
	{
		if (!dont_use_costume_constants && eaSize(&pGeo->eaMatConstant[0]))
		{
			PERFINFO_AUTO_START_L3("named material constants",1);
			FOR_EACH_IN_EARRAY(pGeo->eaMatConstant[0], const MaterialNamedConstant, mnc)
			{
				if (imncIndex > 0)
					PREFETCH(pGeo->eaMatConstant[0][imncIndex - 1]);
				if (mnc->name == s_pcColor0 || (draw_params->wl_params.mod_color_on_all_costume_colors && (mnc->name == s_pcColor1 || mnc->name == s_pcColor2 || mnc->name == s_pcColor3)))
				{
					mulVecVec3(mnc->value, instance_params->instance.color, instance_params->instance.color);
					temp_alpha *= mnc->value[3];
				}
			}
			FOR_EACH_END;
			PERFINFO_AUTO_STOP_L3();
		}
	}
	else
	{
		if (!dont_use_costume_constants && eaSize(&pGeo->eaMatConstant[0]))
		{
			PERFINFO_AUTO_START_L3("named material constants",1);
			FOR_EACH_IN_EARRAY(pGeo->eaMatConstant[0], const MaterialNamedConstant, mnc)
			{
				MaterialNamedConstant* pNewMNC;
				if (imncIndex > 0)
					PREFETCH(pGeo->eaMatConstant[0][imncIndex - 1]);
				pNewMNC = ScratchAlloc(sizeof(MaterialNamedConstant));
				pNewMNC->name = mnc->name;
				if (mnc->name == s_pcColor0 || (draw_params->wl_params.mod_color_on_all_costume_colors && (mnc->name == s_pcColor1 || mnc->name == s_pcColor2 || mnc->name == s_pcColor3)))
				{
					mulVecVec3(mnc->value, instance_params->instance.color, instance_params->instance.color);
					temp_alpha *= mnc->value[3];
					mulVecVec3(mnc->value, draw_params->wl_params.color, pNewMNC->value);
				}
				else
					copyVec3(mnc->value, pNewMNC->value);
				pNewMNC->value[3] = mnc->value[3];
				eaPush(peaMNCCopy, pNewMNC);
			}
			FOR_EACH_END;


			PERFINFO_AUTO_STOP_L3();
		}

		if (peaMNCCopy2 && color2 && pGeo->pMaterial[1])
		{
			if (!dont_use_costume_constants && eaSize(&pGeo->eaMatConstant[1]))
			{
				PERFINFO_AUTO_START_L3("named material constants",1);
				FOR_EACH_IN_EARRAY(pGeo->eaMatConstant[1], const MaterialNamedConstant, mnc)
				{
					MaterialNamedConstant* pNewMNC = ScratchAlloc(sizeof(MaterialNamedConstant));
					pNewMNC->name = mnc->name;
					if (mnc->name == s_pcColor0 || (draw_params->wl_params.mod_color_on_all_costume_colors && (mnc->name == s_pcColor1 || mnc->name == s_pcColor2 || mnc->name == s_pcColor3)))
					{
						mulVecVec3(mnc->value, color2, color2);
						temp_alpha *= mnc->value[3];
						mulVecVec3(mnc->value, draw_params->wl_params.color, pNewMNC->value);
					}
					else
						copyVec3(mnc->value, pNewMNC->value);
					pNewMNC->value[3] = mnc->value[3];
					eaPush(peaMNCCopy2, pNewMNC);
				}
				FOR_EACH_END;


				PERFINFO_AUTO_STOP_L3();
			}
		}

		if (draw_params->wl_params.current_material_swap && draw_params->wl_params.current_material_swap->material_to_use)
			temp_alpha *= draw_params->wl_params.current_material_swap->alpha;

		if (draw_params->wl_params.current_material_swap && draw_params->wl_params.current_material_swap->use_fx_constants)
		{
			int k;
			for (k=0; k<3; ++k)
			{
				if (draw_params->wl_params.current_material_swap->use_mnc[k])
				{
					bool bWritten = false;
					FOR_EACH_IN_EARRAY(*peaMNCCopy, MaterialNamedConstant, pMNC)
					{
						if (pMNC->name == draw_params->wl_params.current_material_swap->mnc[k].name)
						{
							// Override
							copyVec4(draw_params->wl_params.current_material_swap->mnc[k].value, pMNC->value);
							bWritten = true;
						}
					}
					FOR_EACH_END;
					if (!bWritten)
					{
						MaterialNamedConstant* pNewMNC = ScratchAlloc(sizeof(MaterialNamedConstant));
						pNewMNC->name = draw_params->wl_params.current_material_swap->mnc[k].name;
						copyVec4(draw_params->wl_params.current_material_swap->mnc[k].value, pNewMNC->value);
						eaPush(peaMNCCopy, pNewMNC);
					}
				}
			}

			if (peaMNCCopy2 && pGeo->pMaterial[1])
			{
				for (k=0; k<3; ++k)
				{
					if (draw_params->wl_params.current_material_swap->use_mnc[k])
					{
						bool bWritten = false;
						FOR_EACH_IN_EARRAY(*peaMNCCopy2, MaterialNamedConstant, pMNC)
						{
							if (pMNC->name == draw_params->wl_params.current_material_swap->mnc[k].name)
							{
								// Override
								copyVec4(draw_params->wl_params.current_material_swap->mnc[k].value, pMNC->value);
								bWritten = true;
							}
						}
						FOR_EACH_END;
						if (!bWritten)
						{
							MaterialNamedConstant* pNewMNC = ScratchAlloc(sizeof(MaterialNamedConstant));
							pNewMNC->name = draw_params->wl_params.current_material_swap->mnc[k].name;
							copyVec4(draw_params->wl_params.current_material_swap->mnc[k].value, pNewMNC->value);
							eaPush(peaMNCCopy2, pNewMNC);
						}
					}
				}
			}
		}

		// Check for texture swaps
		for (i=0; i<2; ++i)
		{
			if ( eaSize(&pGeo->eaCostumeTextureSwaps[i]) && !pGeo->eaTextureSwaps[i] )
			{
				// Lookup texture swaps and store them on the geo, for the future
				int iTexSwapIndex;
				int iNumTexSwaps = eaSize(&pGeo->eaCostumeTextureSwaps[i]);

				PERFINFO_AUTO_START_L3("texture swaps",1);
				for (iTexSwapIndex=0; iTexSwapIndex<iNumTexSwaps; ++iTexSwapIndex)
				{
					const CostumeTextureSwap* pSwap = pGeo->eaCostumeTextureSwaps[i][iTexSwapIndex];
					BasicTexture* pTexOld = texFind(pSwap->pcOldTexture, true);
					BasicTexture* pTexNew = texFind(pSwap->pcNewTexture?pSwap->pcNewTexture:pSwap->pcNewTextureNonPooled, true);
					
					if ( pTexOld && pTexNew )
					{
						// Add the pair
						eaPush(&pGeo->eaTextureSwaps[i], pTexOld);
						eaPush(&pGeo->eaTextureSwaps[i], pTexNew);
					}
				}
				PERFINFO_AUTO_STOP_L3();
			}
		}

		if (pGeo->bLOD && !pGeo->bHasBodysockSwapSet)
		{
			BasicTexture* pBodySockTexOld = texFind("Color_Spectrum_Pixels", true);
			if (pBodySockTexOld)
			{
				// this must be ready
				if (!pCostume->bBodysockTexCreated)
					dynDrawSkeletonSetupBodysockTexture(pCostume);
				if (!pCostume->pBodysockTexture)
				{
					Errorf("Drawing bodysock model before texture ready.");
					return false;
				}

#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
				logBasicTexUse(pCostume->pBodysockTexture, pDrawSkeleton);
				assert(!texIsFreed(pCostume->pBodysockTexture));
#endif
				eaPush(&pGeo->eaTextureSwaps[0], pBodySockTexOld);
				eaPush(&pGeo->eaTextureSwaps[0], pCostume->pBodysockTexture);
				pGeo->bHasBodysockSwapSet = true;
			}
		}
	}

	temp_alpha *= draw_params->wl_params.alpha;
	if(pCostume)
	{
		temp_alpha *= pDrawSkeleton->fTotalAlpha * pDrawSkeleton->fGeometryOnlyAlpha;
		if (temp_alpha < 1)
			instance_params->need_dof = 1; // This entity is being procedurally faded out, keep it in the DOF pass
	}

	*alpha = temp_alpha;

	if (draw_params->wl_params.force_no_alpha)
		*alpha = 1.0f;

	if (draw_params->frustum_visible & gdraw->visual_frustum_bit)
	{
		instance_params->screen_area = draw_params->fScreenArea;
		fillLightInfo(
			instance_params, draw_params,
			pDrawSkeleton->bForceUnlit ? NULL : pDrawSkeleton->pLightCache,
			dest_light_params);
	}

	return true;
}

static WorldDrawableList **drawable_list_set = NULL;

__forceinline static void gfxDynDrawModelQueueDrawableListClear(WorldDrawableList *pDrawableList)
{
	eaPush(&drawable_list_set, pDrawableList);
}

static void gfxClearDrawableListTempRenderInfo(WorldDrawableList *pDrawableList)
{
	int i, j, k;
	for (i = 0; i < pDrawableList->lod_count; ++i)
	{
		WorldDrawableLod *drawable = &pDrawableList->drawable_lods[i];
		for (j = 0; j < drawable->subobject_count; ++j)
		{
			drawable->subobjects[j]->temp_rdr_subobject = NULL;
			for (k=eaSize(&drawable->subobjects[j]->material_draws)-1; k>=0; k--)
				drawable->subobjects[j]->material_draws[k]->temp_render_info = NULL;
			if (drawable->subobjects[j]->model)
				drawable->subobjects[j]->model->temp_geo_draw = NULL;
		}
	}
}

void gfxDynDrawModelClearTempMaterials(bool bClearGeos)
{
	int i;
	for (i = 0; i < eaSize(&drawable_list_set); ++i)
		gfxClearDrawableListTempRenderInfo(drawable_list_set[i]);
	eaClear(&drawable_list_set);
	worldDrawableListPoolClearMaterialCache(&fx_drawable_pool);
	if (bClearGeos)
		worldDrawableListPoolClearGeoCache(&fx_drawable_pool);
}

__forceinline static bool isMaterialAnimated(const DynDrawParams *draw_params_in)
{
	return !!draw_params_in->wl_params.current_material_swap;
}


static void gfxQueueDynModelInternal(DynDrawSkeleton *pDrawSkeleton, DynDrawModel* pGeo, F32 fDist, DynDrawParams *draw_params_in)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	DynDrawParams draw_params = *draw_params_in;
	static MaterialNamedConstant** eaMNCCopy = NULL;
	int i, j;
	RdrAddInstanceParams instance_params={0};
	RdrLightParams local_light_params = {0};
	ModelToDraw models[NUM_MODELTODRAWS];
	int model_count;
	F32 alpha = 1;
	WLCostume* pCostume = GET_REF(pDrawSkeleton->pSkeleton->hCostume);
	WorldDrawableList *pDrawableList = NULL;
	WorldInstanceParamList *pInstanceParamList = NULL;
	Model *pModel = pGeo->pModel;
	F32 zdist, fTexDist;
	WLFXMaterialSwap* current_swap = draw_params.wl_params.current_material_swap;
	static BasicTexture** eaTextureSwaps = NULL;
	bool bFoundSkinnedData = false;

	// Lazy init the materials
	dynDrawModelInitMaterialPointers(pGeo);

	/*
	if (pCostume && pCostume->bHasLOD && pGeo->bLOD != pDrawSkeleton->pSkeleton->bBodySockDistance)
		return;
		*/

	PERFINFO_AUTO_START_FUNC_L2();

	if (pGeo->pDrawableList && pGeo->iDrawableResetCounter == worldGetResetCount(true)) // protect against dereferencing freed drawables
	{
		pDrawableList = pGeo->pDrawableList;
		pInstanceParamList = pGeo->pInstanceParamList;
	}

	if (pDrawableList && pGeo->bOwnsDrawables && gfx_state.debug.dynDrawNoPreSwappedMaterials)
	{
		pDrawableList = NULL;
		pInstanceParamList = NULL;
	}

	if (isMaterialAnimated(draw_params_in))
	{
		pDrawableList = NULL;
		pInstanceParamList = NULL;
	}

	// use the LOD specified on the skeleton
	if (pDrawableList)
	{
		U32 geo_lod;
		if (pGeo->bLOD)
			geo_lod = 0;
		else if (pDrawableList->lod_count > 2)
			geo_lod = MAX((int)pDrawSkeleton->uiLODLevel-1, 0);
		else
			geo_lod = MAX((int)pDrawSkeleton->uiLODLevel-2, 0);

		model_count = gfxDemandLoadPreSwappedModel(pDrawableList, models, ARRAY_SIZE(models), 0, 1, geo_lod, NULL, pModel->radius, false);
	}
	else
	{
		U32 geo_lod;
		if (pGeo->bLOD)
			geo_lod = 0;
		else if (pModel->lod_info && eaSize(&pModel->lod_info->lods) > 2)
			geo_lod = MAX((int)pDrawSkeleton->uiLODLevel-1, 0);
		else
			geo_lod = MAX((int)pDrawSkeleton->uiLODLevel-2, 0);

		model_count = gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), 0, 1, geo_lod, NULL, pModel->radius);
	}

	for (j=0; j<model_count; j++)
	{
		if (models[j].lod_index >= pGeo->uiNumLODLevels)
		{
			ErrorDetailsf("Model: %s, LODIndex: %d, DynDrawModelLODCount: %d", models[j].model->debug_name, models[j].lod_index, pGeo->uiNumLODLevels);
			Errorf("Got invalid lod index from gfxDemandLoadModel for our DynDrawModel");
			return;
		}
		assert(models[j].lod_index < MAX_DRAW_MODEL_LODS);
		if (pGeo->uiNumNodesUsed[models[j].lod_index] > 0)
			bFoundSkinnedData = true;
	}

	if (!model_count)
	{
		PERFINFO_AUTO_STOP_L2();
		return;
	}


	// Handle body sock color exchange, if necessary
	if (pGeo->bLOD && pCostume)
	{
		MaterialNamedConstant* pNewMNC = ScratchAlloc(sizeof(MaterialNamedConstant));
		pNewMNC->name = s_pcTexXfrm;
		copyVec4(pCostume->vBodysockTexXfrm, pNewMNC->value);
		eaPush(&eaMNCCopy, pNewMNC);
	}


	// Calculate world mid and min/max
	if ( pGeo->pAttachmentNode )
		dynNodeGetWorldSpacePos(pGeo->pAttachmentNode,instance_params.instance.world_mid);
	else
		zeroVec3(instance_params.instance.world_mid); // Couldn't find any node to attach this to, i guess put it at the origin

	zdist = rdrDrawListCalcZDist(gdraw->draw_list, instance_params.instance.world_mid);
	if(gfx_state.texLoadNearCamFocus) {

		// Use the distance from the player or camera focus, times a
		// scaling factor, if it's less than the real camera distance,
		// for the texture distance.
		F32 fFocusDist = distance3(instance_params.instance.world_mid, gfx_state.currentCameraFocus);
		fFocusDist *= 2;
		zdist = MINF(zdist, fFocusDist);
	}

	fTexDist = zdist - pDrawSkeleton->pSkeleton->fStaticVisibilityRadius; // Better place to get this from?

	if (bFoundSkinnedData)
	{
		copyMat3(unitmat, instance_params.instance.world_matrix);
		addVec3(pGeo->vBaseAttachOffset, gdraw->pos_offset, instance_params.instance.world_matrix[3]);
	}
	else
	{
		Mat4 mNodeMatrix;
		dynNodeGetWorldSpaceMat(pGeo->pAttachmentNode, mNodeMatrix, true);
		mulMat4Inline(mNodeMatrix, pGeo->mTransform, instance_params.instance.world_matrix);
		addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);
	}

	// Set up the various drawing params
	if (!gfxPrepareModelDrawParams(
			&draw_params, &instance_params, pGeo, 
			pDrawSkeleton, NULL, &alpha, &eaMNCCopy, NULL, pCostume, pDrawableList != NULL,
			&local_light_params))
	{
		eaClearEx(&eaMNCCopy, ScratchFree);
		eaClearFast(&eaTextureSwaps);
		PERFINFO_AUTO_STOP_L2();
		return; // Abort!
	}

	// enable this code to create the pre-swapped draw list for the character model
	if (!pDrawableList && !draw_params.noMaterialPreSwap && !pGeo->pDrawableList && !gfx_state.debug.dynDrawNoPreSwappedMaterials &&
		!isMaterialAnimated(draw_params_in))
	{
		const char **eaTexSwapStrings = NULL, **eaMaterialSwapStrings = NULL;
		Material *use_material = pGeo->pMaterial[0];
		MaterialNamedTexture bodysock_swap;
		MaterialNamedTexture **eaBodysockSwap = NULL;

		if (pGeo->bLOD && pCostume)
		{
			if (eaSize(&pGeo->eaTextureSwaps[0]))
			{
				bodysock_swap.op = allocAddString("DiffuseMap");
				bodysock_swap.input = allocAddString("Texture");
				bodysock_swap.texture_name = NULL;
				bodysock_swap.texture = pGeo->eaTextureSwaps[0][1];

				eaPush(&eaBodysockSwap, &bodysock_swap);
			}
		}
		else
		{
			int k;
			for (k=0; k<2; ++k)
			{
				FOR_EACH_IN_EARRAY_FORWARDS(pGeo->eaCostumeTextureSwaps[k], CostumeTextureSwap, pSwap)
				{
					eaPush(&eaTexSwapStrings, pSwap->pcOldTexture);
					eaPush(&eaTexSwapStrings, pSwap->pcNewTexture?pSwap->pcNewTexture:pSwap->pcNewTextureNonPooled);
				}
				FOR_EACH_END;
			}
		}

		assert(!pGeo->pInstanceParamList);

		pDrawableList = worldCreateDrawableList(pGeo->pModel, NULL, NULL, NULL, 
			use_material ? use_material->material_name : NULL,
			&eaTexSwapStrings,
			&eaMaterialSwapStrings,
			eaBodysockSwap,
			eaMNCCopy,
			NULL,
			DONT_WAIT_FOR_LOAD,
			WL_FOR_ENTITY,
			&pGeo->pInstanceParamList);

		if (pDrawableList)
		{
			assert(pGeo->pInstanceParamList);
			pGeo->pDrawableList = pDrawableList;
			pGeo->bOwnsDrawables = 1;
		} else {
			assert(!pGeo->pInstanceParamList); // Otherwise need to free it?
		}

		eaDestroy(&eaMaterialSwapStrings);
		eaDestroy(&eaTexSwapStrings);
		eaDestroy(&eaBodysockSwap);
	}

	if (pDrawableList && pGeo->bOwnsDrawables)
		gfxDynDrawModelQueueDrawableListClear(pDrawableList);

	instance_params.no_zprepass = (!pDrawableList || pGeo->bOwnsDrawables) && // not a world interaction entity
								  (pDrawSkeleton->uiLODLevel > 1 || fDist >= gfx_state.debug.onepassDistance) && // force onepass on every skeleton greater than LOD 1
								  !gfx_state.debug.noOnepassObjects && !pDrawSkeleton->bIsLocalPlayer;

	instance_params.two_bone_skinning = pDrawSkeleton->uiLODLevel > 1; // use two bone skinning on every skeleton greater than LOD 1
	if (pDrawSkeleton->bIsLocalPlayer)
	{
		instance_params.force_hdr_pass = 1;
		if (isProductionMode()) // This makes it look much better for just your character, we do *not* want this on for artists, otherwise they'd never notice the horrible things that go wrong with it ^_^
			instance_params.no_two_bone_skinning = 1;
	}

	if (gdraw->do_aux_visual_pass)
		instance_params.aux_visual_pass |= draw_params.wl_params.draw_in_aux_visual_pass;
	else if (current_swap && draw_params.wl_params.draw_in_aux_visual_pass)
		instance_params.distance_offset -= 1.0e-4f;

#if ENABLE_TEXTURE_LIFETIME_EXTENDED_TRACE
	if (pCostume->pBodysockTexture)
	{
		//logBasicTexUse(pCostume->pBodysockTexture, pDrawSkeleton);
		assert(!texIsFreed(pCostume->pBodysockTexture));
	}
#endif

	if (current_swap && current_swap->do_texture_swap)
	{
		eaCopy(&eaTextureSwaps, &pGeo->eaTextureSwaps[0]);
		if (!(*current_swap->texture_swaps[0]))
			*current_swap->texture_swaps[0] = texLoadBasic(current_swap->texture_swap_names[0], TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);
		if (!(*current_swap->texture_swaps[1]))
			*current_swap->texture_swaps[1] = texLoadBasic(current_swap->texture_swap_names[1], TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);

		if (*current_swap->texture_swaps[0] && *current_swap->texture_swaps[1])
		{
			eaPush(&eaTextureSwaps, *current_swap->texture_swaps[0]);
			eaPush(&eaTextureSwaps, *current_swap->texture_swaps[1]);
		}


	}

	// Draw each LOD model
	for (j=0; j<model_count; j++)
	{
		WorldDrawableLod *lod = models[j].draw;
		RdrDrawableGeo *geo_draw = lod ? lod->subobjects[0]->model->temp_geo_draw : NULL;
		RdrDrawableSkinnedModel *skin_draw = NULL;
		RdrInstancePerDrawableData per_drawable_data;

		if (!models[j].geo_handle_primary) {
			assert(0);
			continue;
		}

		instance_params.instance.color[3] = alpha * models[j].alpha;

		if (pGeo->uiNumNodesUsed[models[j].lod_index] > 0 && pDrawSkeleton->pCurrentSkinningMatSet && pDrawSkeleton->pCurrentSkinningMatSet->pSkinningMats)
		{
			if (skin_draw = rdrDrawListAllocSkinnedModel(gdraw->draw_list, RTYPE_SKINNED_MODEL, models[j].model, models[j].model->geo_render_info->subobject_count, 0, pGeo->uiNumNodesUsed[models[j].lod_index], pDrawSkeleton->pCurrentSkinningMatSet->pSkinningMats))
			{
				PERFINFO_AUTO_START_L3("copy bone matrices",1);
				memcpy_fast(skin_draw->skinning_mat_indices, pGeo->apuiBoneIdxs[models[j].lod_index], sizeof(U8) * skin_draw->num_bones);
				PERFINFO_AUTO_STOP_L3();
				geo_draw = &skin_draw->base_geo_drawable;
				dynDebugState.frameCounters.uiNumDrawnSkinnedModels += geo_draw->subobject_count;
			}
		}
		else if (!geo_draw)
		{
			geo_draw = rdrDrawListAllocGeo(gdraw->draw_list, RTYPE_MODEL, models[j].model, models[j].model->geo_render_info->subobject_count, 0, 0);
			if (geo_draw)
			{
				dynDebugState.frameCounters.uiNumDrawnModels += geo_draw->subobject_count;
				if (lod)
					lod->subobjects[0]->model->temp_geo_draw = geo_draw;
			}
		}
		else
		{
			dynDebugState.frameCounters.uiNumInstancedModels += geo_draw->subobject_count;
		}

		if (!geo_draw)
			continue;

		geo_draw->geo_handle_primary = models[j].geo_handle_primary;

		if (lod)
		{
			SETUP_INSTANCE_PARAMS;
			RDRALLOC_SUBOBJECT_PTRS(instance_params, geo_draw->subobject_count);
			for (i = 0; i < geo_draw->subobject_count; i++)
			{
				if (i < geo_draw->subobject_count - 1)
					PREFETCH(lod->subobjects[i + 1]);
				instance_params.subobjects[i] = lod->subobjects[i]->temp_rdr_subobject;
				if (!instance_params.subobjects[i])
				{
					instance_params.subobjects[i] = lod->subobjects[i]->temp_rdr_subobject = rdrDrawListAllocSubobject(gdraw->draw_list, models[j].model->data->tex_idx[i].count);
					lod->subobjects[i]->fallback_idx = getMaterialDrawIndex(lod->subobjects[i]->material_draws);
					lod->subobjects[i]->material_render_info = gfxDemandLoadPreSwappedMaterialAtQueueTime(instance_params.subobjects[i], lod->subobjects[i]->material_draws[lod->subobjects[i]->fallback_idx], fTexDist, models[j].model->uv_density);
				}
				else
				{
					MIN1F(lod->subobjects[i]->material_render_info->material_min_draw_dist[gfx_state.currentAction->action_type], fTexDist);
					MIN1F(lod->subobjects[i]->material_render_info->material_min_uv_density, models[j].model->uv_density);
				}
				if (pInstanceParamList)
					copyVec4(pInstanceParamList->lod_params[models[j].lod_index].subobject_params[i].fallback_params[lod->subobjects[i]->fallback_idx].instance_param, instance_params.per_drawable_data[i].instance_param);
				else
					zeroVec4(instance_params.per_drawable_data[i].instance_param);
			}
		}
		else
		{
			SETUP_INSTANCE_PARAMS;
			RDRALLOC_SUBOBJECTS(gdraw->draw_list, instance_params, models[j].model, i);
			for (i = 0; i < geo_draw->subobject_count; i++)
			{
				if (current_swap && current_swap->material_to_use)
				{
					gfxDemandLoadMaterialAtQueueTime(
						instance_params.subobjects[i],
						current_swap->material_to_use,
						current_swap->dont_use_costume_constants?NULL:pGeo->eaTextureSwaps[0],
						(current_swap->dont_use_costume_constants && !current_swap->use_fx_constants)?NULL:eaMNCCopy,// pGeo->peaMatConstant?(*pGeo->peaMatConstant):NULL
						NULL, NULL,
						instance_params.per_drawable_data[i].instance_param, fTexDist, models[j].model->uv_density
						);

				}
				else
				{
					gfxDemandLoadMaterialAtQueueTime(
						instance_params.subobjects[i],
						pGeo->pMaterial[0]?pGeo->pMaterial[0]:models[j].model->materials[i],
						(current_swap && current_swap->do_texture_swap)?eaTextureSwaps:pGeo->eaTextureSwaps[0],
						eaMNCCopy,// pGeo->peaMatConstant?(*pGeo->peaMatConstant):NULL
						NULL, NULL,
						instance_params.per_drawable_data[i].instance_param, fTexDist, models[j].model->uv_density
						);

				}
			}
		}

		instance_params.add_material_flags = draw_params.additional_material_flags;

		if (skin_draw)
			rdrDrawListAddSkinnedModel(gdraw->draw_list, skin_draw, &instance_params, RST_AUTO, ROC_CHARACTER);
		else
			rdrDrawListAddGeoInstance(gdraw->draw_list, geo_draw, &instance_params, RST_AUTO, ROC_CHARACTER, true);
		draw_params.queued_as_alpha = instance_params.queued_as_alpha;

		if (instance_params.uniqueDrawCount)
			gfxGeoIncrementUsedCount(models[j].model->geo_render_info, instance_params.uniqueDrawCount, true);
	}

	// send back one flag to the caller
	draw_params_in->queued_as_alpha = draw_params.queued_as_alpha;

	eaClearEx(&eaMNCCopy, ScratchFree);
	eaClearFast(&eaTextureSwaps);
	PERFINFO_AUTO_STOP_L2();
}


__forceinline static void gfxQueueDynModel(DynDrawSkeleton *pDrawSkeleton, DynDrawModel* pGeo, F32 fDist, DynDrawParams *draw_params_in)
{
#if _XBOX && PROFILE
	if (gfx_state.debug.danimPIXLabelModels)
		PIXBeginNamedEvent(0, "%s", pGeo->pModel->name);
#endif

	gfxQueueDynModelInternal(pDrawSkeleton, pGeo, fDist, draw_params_in);

#if _XBOX && PROFILE
	if (gfx_state.debug.danimPIXLabelModels)
		PIXEndNamedEvent();
#endif
}

int checkClothVerts = 1;
AUTO_CMD_INT(checkClothVerts,checkClothVerts);

// Cloth tessellation helper.
// Get the index for a specific edge on a specific triangle.
static int getIndexForClothEdge(DynCloth *cloth, int triangle, int idx1, int idx2) {
	int edge;

	// numDefinedEdges is at most 3.
	for(edge = 0; edge < cloth->renderData.edgesByTriangle[triangle].numDefinedEdges; edge++) {

		int edgeNum = cloth->renderData.edgesByTriangle[triangle].edges[edge];
		int edgeIdx1 = cloth->renderData.edges[edgeNum].idx[0];
		int edgeIdx2 = cloth->renderData.edges[edgeNum].idx[1];

		if(	(edgeIdx1 == idx1 && edgeIdx2 == idx2) ||
			(edgeIdx2 == idx1 && edgeIdx1 == idx2)) {

			return edgeNum;
		}
	}

	// That edge isn't on this triangle?
	return -1;
}

// pModel, pMaterial, vInstanceColor, and vPos are used in the absence of a pGeo, but ignored if one is there.
static void gfxQueueDynClothMeshEx(DynDrawParams *draw_params_in, const DynClothObject* pCloth, DynDrawSkeleton* pDrawSkeleton, DynDrawModel* pGeo, Model *pModel, Material *pMaterial, Material *pMaterialBack, Vec4 vInstanceColor, Vec3 vPos, BasicTexture** eaTexSwaps)
{
	DynClothMesh *pMesh;
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	DynDrawParams draw_params = *draw_params_in;
	RdrAddInstanceParams instance_params = {0};
	RdrInstancePerDrawableData per_drawable_data;
	Vec3 back_color = { 1, 1, 1 };
	F32 alpha = 1.0f;
	F32 zdist;
	int i;
	WLCostume* pCostume = NULL;
	static MaterialNamedConstant** eaMNCCopy[2] = { 0 };
	RdrLightParams light_params;

	if(dynDebugState.cloth.bDisableCloth) {
		return;
	}

	// If this cloth hasn't even received an update yet, it'll do something silly like render at the origin instead of
	// where it really needs to be, so just skip it for now. Or worse, it'll just crash.
	if(pCloth->iLastLOD == -1 || pCloth->iCurrentLOD == -1) return;

	pMesh = pCloth->eaLODs[pCloth->iCurrentLOD]->pMesh;

	PERFINFO_AUTO_START_FUNC();

	if(pDrawSkeleton) {
		pCostume = GET_REF(pDrawSkeleton->pSkeleton->hCostume);
	}

	if(pGeo) {

		dynDrawModelInitMaterialPointers(pGeo);

		// Calculate world position
		dynNodeGetWorldSpaceMat(pGeo->pAttachmentNode, instance_params.instance.world_matrix, false);
		addVec3(instance_params.instance.world_matrix[3], gdraw->pos_offset, instance_params.instance.world_matrix[3]);
	
	} else {

		// No pGeo - use passed-in position.
		identityMat4(instance_params.instance.world_matrix);
		copyVec3(vPos, instance_params.instance.world_matrix[3]);
	}

	copyVec3(instance_params.instance.world_matrix[3], instance_params.instance.world_mid);

	zdist = rdrDrawListCalcZDist(gdraw->draw_list, instance_params.instance.world_mid);

	// Set up the various drawing params
	if(pGeo) {

		if (!gfxPrepareModelDrawParams(&draw_params, &instance_params, pGeo, pDrawSkeleton, back_color, &alpha, &eaMNCCopy[0], &eaMNCCopy[1], pCostume, false, NULL))
		{
			for (i=0; i<2; ++i)
			{
				eaClearEx(&eaMNCCopy[i], ScratchFree);
				eaSetSize(&eaMNCCopy[i], 0);
			}
			PERFINFO_AUTO_STOP_FUNC();
			return;
		}

	} else {

		Vec3 world_min;
		Vec3 world_max;

		instance_params.wireframe = gfx_state.wireframe & 3;

		gfxGetObjectLightsUncached(&light_params, NULL, instance_params.instance.world_mid, pModel->radius, true);
		instance_params.light_params = &light_params;
		instance_params.frustum_visible = draw_params_in->frustum_visible;
	
		scaleAddVec3(onevec3, pModel->radius, instance_params.instance.world_mid, world_min);
		scaleAddVec3(onevec3, -pModel->radius, instance_params.instance.world_mid, world_max);
		
		for (i = 0; i < gdraw->pass_count; ++i) {
			if (draw_params.frustum_visible & gdraw->passes[i]->frustum_set_bit)
				frustumUpdateBounds(gdraw->passes[i]->frustum, world_min, world_max);
		}
	}

	setVec3same(instance_params.ambient_multiplier, 1);

	// Do the actual drawing
	{
		RdrDrawableClothMesh* pRdrMesh;
		RdrDrawableGeo* geo_draw;
		S32 iTriCount = 0;
		ModelLOD *model_lod = modelGetLOD(pModel, 0);
		WLFXMaterialSwap* current_swap = draw_params.wl_params.current_material_swap;
		int materialCount = 1;
		
		if(pGeo && pGeo->pMaterial[1]) {
			materialCount = 2;
		}

		if(pMaterial && pMaterialBack) {
			materialCount = 2;
		}

		if (pMesh && (pRdrMesh = rdrDrawListAllocClothMesh(gdraw->draw_list, RTYPE_CLOTHMESH, model_lod, pMesh->NumPoints + ((pCloth->bTessellate) ? pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth->renderData.numEdges : 0), pMesh->NumStrips, materialCount, 0)))
		{
			geo_draw = &pRdrMesh->base_geo_drawable;

			dynDebugState.frameCounters.uiNumDrawnClothMeshes += pRdrMesh->strip_count * pRdrMesh->base_geo_drawable.subobject_count;

			// Set up each strip
			{
				S32 iStrip;
				for (iStrip = 0; iStrip<pMesh->NumStrips; ++iStrip)
				{
					DynClothStrip* pStrip = &pMesh->Strips[iStrip];
					RdrDrawableIndexedTriStrip* pRdrStrip = rdrDrawListAllocClothMeshStripIndices(gdraw->draw_list, pRdrMesh, iStrip, pCloth->bTessellate ? (pStrip->NumIndices * 4) : pStrip->NumIndices);
					pRdrStrip->max_index = pStrip->MaxIndex;
					pRdrStrip->min_index = pStrip->MinIndex;
					iTriCount += pStrip->NumIndices/3;
				
					if(pCloth->bTessellate) {
						
						int tri;
						int vert = 0;

						// We need to split each triangle.
						for(tri = 0; tri < iTriCount; tri++) {

							// Indices from the original triangle.
							const S16 *origIdx = pStrip->IndicesCCW + (tri * 3);

							// Get edge midpoints.
							int midIdx[3] = {
								pMesh->NumPoints + getIndexForClothEdge(pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth, tri, origIdx[0], origIdx[1]),
								pMesh->NumPoints + getIndexForClothEdge(pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth, tri, origIdx[1], origIdx[2]),
								pMesh->NumPoints + getIndexForClothEdge(pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth, tri, origIdx[2], origIdx[0])};
							
							// From this triangle and the edge
							// midpoints, build the subdivided
							// triangles.

							pRdrStrip->indices[vert++] = origIdx[0];
							pRdrStrip->indices[vert++] = midIdx[0];
							pRdrStrip->indices[vert++] = midIdx[2];
							
							pRdrStrip->indices[vert++] = midIdx[0];
							pRdrStrip->indices[vert++] = origIdx[1];
							pRdrStrip->indices[vert++] = midIdx[1];

							pRdrStrip->indices[vert++] = midIdx[1];
							pRdrStrip->indices[vert++] = origIdx[2];
							pRdrStrip->indices[vert++] = midIdx[2];

							// Center triangle (only midpoints)
							pRdrStrip->indices[vert++] = midIdx[0];
							pRdrStrip->indices[vert++] = midIdx[1];
							pRdrStrip->indices[vert++] = midIdx[2];

						}

					} else {
						
						// No tessellation - just copy the triangle data over.
						memcpy(pRdrStrip->indices, pStrip->IndicesCCW, sizeof(U16) * pStrip->NumIndices);
					}

				}
			}

			// Set up vertex pool
			{
				S32 iVert;
				for (iVert=0; iVert<pMesh->NumPoints; ++iVert)
				{
					RdrClothMeshVertex* pVert = &pRdrMesh->verts[iVert];
					copyVec3(pMesh->Points[iVert], pVert->point);

					copyVec3(pMesh->Normals[iVert], pVert->normal);
					copyVec3(pMesh->BiNormals[iVert], pVert->binormal);
					copyVec3(pMesh->Tangents[iVert], pVert->tangent);
					
					copyVec2(pMesh->TexCoords[iVert], pVert->texcoord);
				}

				if(pCloth->bTessellate) {
					
					// Fix up tessellated points.
					for(iVert = pMesh->NumPoints; iVert < pMesh->NumPoints + pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth->renderData.numEdges; iVert++) {

						Vec3 newPoint;
						Vec3 newNormal;
						Vec3 newBiNormal;
						Vec3 newTangent;
						Vec2 newTexCoords;
						
						RdrClothMeshVertex* pVert = &pRdrMesh->verts[iVert];

						DynClothEdge *edge = &(pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth->renderData.edges[iVert - pMesh->NumPoints]);

						int idx1 = edge->idx[0];
						int idx2 = edge->idx[1];

						// Calculate new point's location (before being
						// offset for extra smoothness).
						scaleVec3(pMesh->Points[idx1], 0.5, newPoint);
						scaleAddVec3(pMesh->Points[idx2], 0.5, newPoint, newPoint);
						copyVec3(newPoint, pVert->point);

						// Normal - averaged and normalized.
						scaleVec3(pMesh->Normals[idx1], 0.5, newNormal);
						scaleAddVec3(pMesh->Normals[idx2], 0.5, newNormal, newNormal);
						normalVec3(newNormal);
						copyVec3(newNormal, pVert->normal);

						// Tangent (from BiNormals, as the old cloth system did).
						scaleVec3(pMesh->BiNormals[idx1], 0.5, newBiNormal);
						scaleAddVec3(pMesh->BiNormals[idx2], 0.5, newBiNormal, newBiNormal);
						scaleVec3(newBiNormal, 1, newBiNormal);
						normalVec3(newBiNormal);
						copyVec3(newBiNormal, pVert->binormal);

						// BiNormal (from Tangents, as the old cloth system did).
						scaleVec3(pMesh->Tangents[idx1], 0.5, newTangent);
						scaleAddVec3(pMesh->Tangents[idx2], 0.5, newTangent, newTangent);
						normalVec3(newTangent);
						copyVec3(newTangent, pVert->tangent);

						// Texture coordinates - averaged.
						scaleVec2(pMesh->TexCoords[idx1], 0.5, newTexCoords);
						scaleAddVec2(pMesh->TexCoords[idx2], 0.5, newTexCoords, newTexCoords);
						copyVec2(newTexCoords, pVert->texcoord);

						// Apply smoothing to the interpolated vertices.
						{
							Vec3 posDisplacement;
							Vec3 posDisplace2;
							
							Vec3 pointsDelta; // Vector difference between the two non-interpolated points.
							Vec3 pt1ToMid;    // Vector difference between the first point and the interpolated point.
							float distBetweenPoints; // Distance between NON-INTERPOLATED points.
							float crossLen;

							float avgMass;
							
							if(dynDebugState.cloth.bTessellateAttachments) {
								
								// This will probably make attachments
								// not line up properly. Helps for
								// debugging the tessellation and
								// smoothing, though.
								
								avgMass = 1;

							} else {

								// Particle mass corresponds to
								// attachment points.

								avgMass = (pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth->InvMasses[idx1] + pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth->InvMasses[idx2]) * 0.5f;
								if(avgMass > 1) avgMass = 1;

							}

							subVec3(pMesh->Points[idx1], pMesh->Points[idx2], pointsDelta);
							subVec3(pMesh->Points[idx1], newPoint, pt1ToMid);
							distBetweenPoints = lengthVec3(pointsDelta);
							
							crossVec3(pMesh->Normals[idx1], pMesh->Normals[idx2], posDisplacement);
							crossLen = lengthVec3(posDisplacement);

							crossVec3(posDisplacement, pt1ToMid, posDisplace2);

							// Change displacement amount based on
							// particle mass too. This way we won't
							// move particles that are part of an
							// attachment.
							scaleVec3(posDisplace2, crossLen * avgMass, posDisplacement);

							// Add that onto the interpolated position.
							addVec3(newPoint, posDisplacement, pVert->point);
						}

					}
				}
			}

			if (dynDebugState.cloth.bDrawNormals || dynDebugState.cloth.bDrawTanSpace)
			{
				S32 iVert;
				for (iVert=0; iVert<pMesh->NumPoints + (pCloth->bTessellate ? pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth->renderData.numEdges : 0); ++iVert)
				{
					Vec3 vNormEnd, vBinEnd, vTanEnd;
					Color normCol, binCol, tanCol;
					normCol.integer_for_equality_only = binCol.integer_for_equality_only = tanCol.integer_for_equality_only = 0;
					normCol.a = binCol.a = tanCol.a = 0xFF;
					normCol.r = binCol.g = tanCol.b = 0xFF;

					// FIXME: (As above.) Why does this need the normal inverted?
					scaleAddVec3(pRdrMesh->verts[iVert].normal, -0.3f, pRdrMesh->verts[iVert].point, vNormEnd);
					scaleAddVec3(pRdrMesh->verts[iVert].binormal, 0.3f, pRdrMesh->verts[iVert].point, vBinEnd);
					scaleAddVec3(pRdrMesh->verts[iVert].tangent, 0.3f, pRdrMesh->verts[iVert].point, vTanEnd);
					
					gfxDrawLine3DWidth(pRdrMesh->verts[iVert].point, vNormEnd, normCol, normCol, 0.1f);
					if (dynDebugState.cloth.bDrawTanSpace)
					{
						gfxDrawLine3DWidth(pRdrMesh->verts[iVert].point, vBinEnd, binCol, binCol, 0.1f);
						gfxDrawLine3DWidth(pRdrMesh->verts[iVert].point, vTanEnd, tanCol, tanCol, 0.1f);
					}
				}

			}

			instance_params.instance.color[3] = alpha;
			copyVec3(back_color, pRdrMesh->color2);
			SETUP_INSTANCE_PARAMS;

			// Set up materials

			if(!pMaterialBack) {
				pMaterialBack = pMaterial;

				if(pGeo && geo_draw->subobject_count != 1) {
					SWAPVEC3(pRdrMesh->color2, instance_params.instance.color);
				}
			}

			RDRALLOC_SUBOBJECT_PTRS(instance_params, geo_draw->subobject_count);
			assert(geo_draw->subobject_count < 3); // cloth only supports up to 2 sub objects
			for (i = 0; i < geo_draw->subobject_count; i++)
			{
				int iMatIndex = i<2?i:0;
				int iTexSwapIndex = !i;
				int iMncIndex = !i;

				instance_params.subobjects[i] = rdrDrawListAllocSubobject(gdraw->draw_list, iTriCount);
				
				if(pGeo && pGeo->pMaterial[0] && !pGeo->pMaterial[1]) {
					// Only one material. Override our little switchy
					// hack that makes outer cloth render on low settings.
					// FIXME: Clean up this hideous hack. -Cliff
					iMatIndex = 1;
					iTexSwapIndex = 0;
				}

				if(!eaSize(&eaMNCCopy[1])) {
					iMncIndex = 0;
				}

				if(pMaterial) {

					// Material overridden by passed-in material.
					gfxDemandLoadMaterialAtQueueTime(
						instance_params.subobjects[i],
						(i == 0) ? pMaterial : pMaterialBack,
						eaTexSwaps,
						NULL,
						NULL, NULL,
						instance_params.per_drawable_data[i].instance_param, zdist - pModel->radius,
						pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth->fUvDensity);

				} else if (current_swap) {

					gfxDemandLoadMaterialAtQueueTime(
						instance_params.subobjects[i],
						(current_swap->material_to_use == NULL ? (pGeo ? (pGeo->pMaterial[iMatIndex]?pGeo->pMaterial[iMatIndex]:NULL) : NULL) : current_swap->material_to_use),
						current_swap->dont_use_costume_constants?NULL:(pGeo ? (pGeo->eaTextureSwaps[i]) : NULL),
						(current_swap->dont_use_costume_constants && !current_swap->use_fx_constants)?NULL:eaMNCCopy[i],// pGeo->peaMatConstant?(*pGeo->peaMatConstant):NULL
						NULL, NULL,
						instance_params.per_drawable_data[i].instance_param, zdist - pModel->radius,
						pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth->fUvDensity);
				
				} else {

					gfxDemandLoadMaterialAtQueueTime(
						instance_params.subobjects[i],						
						pGeo ? (pGeo->pMaterial[!iMatIndex]?pGeo->pMaterial[!iMatIndex]:NULL) : NULL,
						pGeo ? (pGeo->eaTextureSwaps[iTexSwapIndex]) : NULL,
						eaMNCCopy[iMncIndex],// pGeo->peaMatConstant?(*pGeo->peaMatConstant):NULL
						NULL, NULL,
						instance_params.per_drawable_data[i].instance_param, zdist - pModel->radius,
						pCloth->eaLODs[pCloth->iCurrentLOD]->pCloth->fUvDensity);

				}
			}

			instance_params.add_material_flags = draw_params.additional_material_flags;

			// Make sure cloth that goes into the aux visual pass doesn't get z
			// prepass turned on. Otherwise the alpha cut out area will appear
			// opaque in the aux pass.
			if (gdraw->do_aux_visual_pass) {
				instance_params.aux_visual_pass |= draw_params.wl_params.draw_in_aux_visual_pass;
				if(instance_params.aux_visual_pass) {
					instance_params.no_zprepass = 1;
				}
			} else if (current_swap && draw_params.wl_params.draw_in_aux_visual_pass)
				instance_params.distance_offset -= 1.0e-4f;

			if(!pGeo) {
				
				// These aren't set up without a pGeo. (World/FX cloth.)
				instance_params.screen_area = draw_params.fScreenArea;
				instance_params.frustum_visible = draw_params.frustum_visible;
				instance_params.distance_offset = pModel->radius;

				copyVec4(vInstanceColor, instance_params.instance.color);
			}

			rdrDrawListAddClothMesh(gdraw->draw_list, pRdrMesh, &instance_params, RST_AUTO, pGeo ? ROC_CHARACTER : ROC_FX);
			draw_params.queued_as_alpha = instance_params.queued_as_alpha;
		}
	}


	// send back one flag to the caller
	draw_params_in->queued_as_alpha = draw_params.queued_as_alpha;

	for (i=0; i<2; ++i)
	{
		eaClearEx(&eaMNCCopy[i], ScratchFree);
		eaSetSize(&eaMNCCopy[i], 0);
	}
	PERFINFO_AUTO_STOP_FUNC();
}

static void gfxQueueDynClothMesh(DynDrawParams *draw_params_in, const DynClothObject* pCloth, DynDrawSkeleton* pDrawSkeleton, DynDrawModel* pGeo) {
	
	Vec4 color = {1, 1, 1, 1};
	Vec3 pos = {0, 0, 0};

	gfxQueueDynClothMeshEx(draw_params_in, pCloth, pDrawSkeleton, pGeo, pGeo->pModel, NULL, NULL, color, pos, NULL);
}

static void gfxQueueClothParticle(DynParticle* pParticle, const DynDrawParams *draw_params_in) {

	DynClothObject *pClothObject = pParticle->pDraw->pCloth;
	DynDrawParams params = *draw_params_in;
	Vec4 color;
	Vec3 pos;
	static BasicTexture** eaTexSwaps = NULL;

	// Set up texture swaps.
	if(pParticle->pDraw->pTexture || pParticle->pDraw->pTexture2) {
		
		if(!pDefaultTexture1)
			pDefaultTexture1 = texLoadBasic(allocAddString("FXTexture1"), TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);

		if(!pDefaultTexture2)
			pDefaultTexture2 = texLoadBasic(allocAddString("FXTexture2"), TEX_LOAD_IN_BACKGROUND, WL_FOR_FX);

		if (pParticle->pDraw->pTexture) {
			eaPush(&eaTexSwaps, pDefaultTexture1);
			eaPush(&eaTexSwaps, pParticle->pDraw->pTexture);
		}

		if (pParticle->pDraw->pTexture2) {
			eaPush(&eaTexSwaps, pDefaultTexture2);
			eaPush(&eaTexSwaps, pParticle->pDraw->pTexture2);
		}
	}

	{
		Vec4 vShiftedColor;

		hsvShiftRGB(
			pParticle->pDraw->vColor,
			vShiftedColor,
			pParticle->pDraw->vHSVShift[0] + pParticle->pDraw->fHueShift, 
			pParticle->pDraw->vHSVShift[1] + pParticle->pDraw->fSaturationShift,
			pParticle->pDraw->vHSVShift[2] + pParticle->pDraw->fValueShift);

		vShiftedColor[3] = pParticle->pDraw->vColor[3];

		scaleVec4(vShiftedColor, 1.0/255.0, color);
	}

	color[3] *= pParticle->fFadeOut;

	copyVec3(pParticle->vOldPos, pos);
	gfxQueueDynClothMeshEx(&params, pClothObject, NULL, NULL, pClothObject->pModel, pParticle->pDraw->pMaterial, pParticle->pDraw->pMaterial2, color, pos, eaTexSwaps);

	eaClear(&eaTexSwaps);
}

static bool ao_do_splats = false;
AUTO_CMD_INT(ao_do_splats, ao_do_splats) ACMD_COMMANDLINE;

static float ao_foot_splat_amt = 0.8f;
static float ao_foot_splat_max = 0.3f;
static float ao_foot_splat_sz = 0.75f;
AUTO_CMD_FLOAT(ao_foot_splat_amt, ao_foot_splat_amt) ACMD_COMMANDLINE;
AUTO_CMD_FLOAT(ao_foot_splat_max, ao_foot_splat_max) ACMD_COMMANDLINE;
AUTO_CMD_FLOAT(ao_foot_splat_sz, ao_foot_splat_sz) ACMD_COMMANDLINE;

static float ao_hip_splat_amt = 0.4f;
static float ao_hip_splat_max = 0.5f;
static float ao_hip_splat_sz_x = 4.0f;
static float ao_hip_splat_sz_y = 2.0f;
AUTO_CMD_FLOAT(ao_hip_splat_amt, ao_hip_splat_amt) ACMD_COMMANDLINE;
AUTO_CMD_FLOAT(ao_hip_splat_max, ao_hip_splat_max) ACMD_COMMANDLINE;
AUTO_CMD_FLOAT(ao_hip_splat_sz_x, ao_hip_splat_sz_x) ACMD_COMMANDLINE;
AUTO_CMD_FLOAT(ao_hip_splat_sz_y, ao_hip_splat_sz_y) ACMD_COMMANDLINE;

static bool danimAssetPreloadAllow = true;
// Allows code that keeps assets preloaded for specified entities
AUTO_CMD_INT(danimAssetPreloadAllow, danimAssetPreloadAllow) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

static F32 danimAssetPreloadDensity = TEX_DEMAND_LOAD_DEFAULT_UV_DENSITY;
// Sets the texel density to use for preloading
AUTO_CMD_FLOAT(danimAssetPreloadDensity, danimAssetPreloadDensity) ACMD_COMMANDLINE ACMD_ACCESSLEVEL(0);

void gfxEnsureAssetsLoadedForSkeleton(DynDrawSkeleton *pDrawSkeleton)
{
	if (!danimAssetPreloadAllow)
		return;

	PERFINFO_AUTO_START_FUNC_L2();
	
	if (!pDrawSkeleton->bPreloadFilledIn) 
	{
		const WLCostume *pCostume = GET_REF(pDrawSkeleton->pSkeleton->hCostume);
		eaClear(&pDrawSkeleton->preload.eaTextures);
		eaClear(&pDrawSkeleton->preload.eaModels);
		FOR_EACH_IN_EARRAY(pCostume->eaCostumeParts, const WLCostumePart, pCostumePart)
		{
			if ( pCostumePart && !pCostumePart->bCollisionOnly )
			{
				U32 iTexIndex;
				int iMatIndex;

				if ( pCostumePart->pchGeometry )
				{
					ModelHeaderSet *pSet = NULL;
					Model *pModel = NULL;
					int model_count;
					ModelToDraw models[NUM_MODELTODRAWS];
					int i;
					int iModelIndex;
					pSet = modelHeaderSetFind(pCostumePart->pchGeometry);
					if (pCostumePart->pcModel)
						pModel = modelFindEx(SAFE_MEMBER(pSet, filename), pCostumePart->pcModel, false, WL_FOR_ENTITY);

					for (i=0; !pModel && pSet && i<eaSize(&pSet->model_headers); ++i)
					{
						ModelHeader *pModelHeader = pSet->model_headers[i];
						if ( eaSize(&pModelHeader->bone_names) > 0 && stricmp(pModelHeader->attachment_bone, pCostumePart->pchBoneName) == 0)
						{
							// Found it
							pModel = modelFromHeader(pModelHeader, false, WL_FOR_ENTITY);
						}
					}
					if (!pModel && pCostumePart && pSet && eaSize(&pSet->model_headers) > 0)
					{
						pModel = modelFromHeader(pSet->model_headers[0], false, WL_FOR_ENTITY);
					}

					if (pModel)
					{
						// Load model and related materials
						eaPush(&pDrawSkeleton->preload.eaModels, pModel);
						model_count = gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), 20, 1, -1, NULL, pModel->radius);
						for (iModelIndex=0; iModelIndex<model_count; iModelIndex++)
						{
							ModelLOD *model_lod = models[iModelIndex].model;
							for (iMatIndex=0; iMatIndex<model_lod->geo_render_info->subobject_count; iMatIndex++)
							{
								// base materials on model
								Material *pMaterial = model_lod->materials[iMatIndex];
								if (!pMaterial->graphic_props.render_info)
									gfxMaterialsInitMaterial(pMaterial, true);
								for (iTexIndex=0; iTexIndex<pMaterial->graphic_props.render_info->rdr_material.tex_count; iTexIndex++)
								{
									eaPushUnique(&pDrawSkeleton->preload.eaTextures, pMaterial->graphic_props.render_info->textures[iTexIndex]);
								}
							}
						}
					}
				}

				if ( pCostumePart->pchMaterial )
				{
					Material *pMaterial = materialFind(pCostumePart->pchMaterial, WL_FOR_ENTITY);
					//eaPushUnique(&pDrawSkeleton->preload.eaMaterials, pMaterial);
					if (!pMaterial->graphic_props.render_info)
						gfxMaterialsInitMaterial(pMaterial, true);
					for (iTexIndex=0; iTexIndex<pMaterial->graphic_props.render_info->rdr_material.tex_count; iTexIndex++)
					{
						eaPushUnique(&pDrawSkeleton->preload.eaTextures, pMaterial->graphic_props.render_info->textures[iTexIndex]);
					}
				}

				if ( pCostumePart->pSecondMaterialInfo && pCostumePart->pSecondMaterialInfo->pchMaterial )
				{
					Material *pMaterial = materialFind(pCostumePart->pSecondMaterialInfo->pchMaterial, WL_FOR_ENTITY);
					//eaPushUnique(&pDrawSkeleton->preload.eaMaterials, pMaterial);
					if (!pMaterial->graphic_props.render_info)
						gfxMaterialsInitMaterial(pMaterial, true);
					for (iTexIndex=0; iTexIndex<pMaterial->graphic_props.render_info->rdr_material.tex_count; iTexIndex++)
					{
						eaPushUnique(&pDrawSkeleton->preload.eaTextures, pMaterial->graphic_props.render_info->textures[iTexIndex]);
					}
				}

				// Texture swaps
				FOR_EACH_IN_EARRAY(pCostumePart->eaTextureSwaps, CostumeTextureSwap, pSwap)
				{
					BasicTexture *btex;
					btex = texFind(pSwap->pcOldTexture, false);
					if (btex)
						eaFindAndRemove(&pDrawSkeleton->preload.eaTextures, btex);
						
					btex = texFind(pSwap->pcNewTexture?pSwap->pcNewTexture:pSwap->pcNewTextureNonPooled, false);
					if (btex)
						eaPushUnique(&pDrawSkeleton->preload.eaTextures, btex);
				}
				FOR_EACH_END;
				FOR_EACH_IN_EARRAY(pCostumePart->eaTextureSwaps2, CostumeTextureSwap, pSwap)
				{
					BasicTexture *btex;
					btex = texFind(pSwap->pcOldTexture, false);
					if (btex)
						eaFindAndRemove(&pDrawSkeleton->preload.eaTextures, btex);
					
					btex = texFind(pSwap->pcNewTexture?pSwap->pcNewTexture:pSwap->pcNewTextureNonPooled, false);
					if (btex)
						eaPushUnique(&pDrawSkeleton->preload.eaTextures, btex);
				}
				FOR_EACH_END;
			}
		}
		FOR_EACH_END;
		pDrawSkeleton->bPreloadFilledIn = 1;
	}
	// Load from the cached list
	{
		ModelToDraw models[NUM_MODELTODRAWS];
		F32 fUvDensity = danimAssetPreloadDensity;
		F32 fTexDist = 0.0f;

		FOR_EACH_IN_EARRAY(pDrawSkeleton->preload.eaModels, Model, pModel)
		{
			// Load model and related materials
			gfxDemandLoadModel(pModel, models, ARRAY_SIZE(models), fTexDist, 1, -1, NULL, pModel->radius);
		}
		FOR_EACH_END;
		FOR_EACH_IN_EARRAY(pDrawSkeleton->preload.eaTextures, BasicTexture, btex)
		{
			texDemandLoadInline(btex, fTexDist, fUvDensity, white_tex);
		}
		FOR_EACH_END;
	}

	PERFINFO_AUTO_STOP_L2();
}

static bool gfxShouldDrawModelAsCloth(DynDrawModel *pGeo, DynDrawSkeleton *pDrawSkeleton) {

	if(!pGeo->bCloth || !pGeo->pCloth) {
		// This thing doesn't even have cloth.
		return false;
	}

	if(dynDebugState.cloth.bDrawClothAsNormalGeo) {
		// Debug option has disabled cloth display.
		return false;
	}

	if(!dynDebugState.cloth.bDisableNormalModelForLowLOD) {

		if(!pDrawSkeleton->pSkeleton->ragdollState.bRagdollOn &&
		   pDrawSkeleton->uiLODLevel > (unsigned int)dynDebugState.cloth.iMaxLOD - 1 &&
		   dynDebugState.cloth.iMaxLOD) {
			// The object is not at an appropriate LOD level to to justify displaying as
			// cloth, or is not in a ragdoll state (which would override the LOD
			// restriction).
			return false;
		}

		if(pGeo->pCloth && pDrawSkeleton->uiLODLevel > (unsigned int)eaSize(&pGeo->pCloth->eaLODs)) {
			// The object's LOD is beyond the range of LODs that have cloth skinning
			// information.
			return false;
		}
	}

	return true;
}

static void gfxQueueSkeleton(DynDrawSkeleton* pDrawSkeleton, const DynDrawParams *draw_params_in)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	U32 uiNumGeos;
	//U32 uiNumNodes = (U32) eaSize(&pDrawSkeleton->eaNodes);
	U32 uiGeoIndex;
	DynDrawParams draw_params = *draw_params_in;
	F32 fDist;
	WLDynDrawParams wl_params_backup;
	U32 shadow_visible_bits;
	Vec3 vWorldMin, vWorldMax;
	int k;
	F32 fNearPlaneFadeOut = 1.0f;
	Vec3 vVisibleMin, vVisibleMax;
	
	if(pDrawSkeleton->bDontDraw)
		return;

	//check to see if this sub-skeleton is parented to a severed bone
	//Wish I could put this in dynDrawSkeletonCreateDbg() instead, but draw skeletons aren't
	// recreated when going in/out of combat.
	if (pDrawSkeleton->pSkeleton								&&
		pDrawSkeleton->pSkeleton->bOwnedByParent				&&
		pDrawSkeleton->pSkeleton->pParentSkeleton				&&
		pDrawSkeleton->pSkeleton->pParentSkeleton->pDrawSkel)
	{
		DynNode* pGrandparent = SAFE_MEMBER2(pDrawSkeleton->pSkeleton->pRoot, pParent, pParent);
		if (pGrandparent && 
			eaFind(&pDrawSkeleton->pSkeleton->pParentSkeleton->pDrawSkel->eaHiddenBoneVisSetBones, pGrandparent->pcTag) >= 0)
		{
			return;
		}
	}
	PERFINFO_AUTO_PIX_START(__FUNCTION__);

	dynNodeGetWorldSpaceMat(pDrawSkeleton->pSkeleton->pRoot, draw_params.root_mat, false);

	// use the genesis skeleton for shadow visibility so we don't get parts of the body (such as the tail) popping on & off based on their animation
	dynSkeletonGetVisibilityExtents(pDrawSkeleton->pSkeleton->pGenesisSkeleton, vVisibleMin, vVisibleMax, true);
	if (gdraw->shadow_frustum_bits && gfxIsThinShadowCaster(vVisibleMin, vVisibleMax)) {
		draw_params.frustum_visible &= ~gdraw->shadow_frustum_bits;
	}

	// now get the bounds for this skeleton & its children for the visibility test below
	dynSkeletonGetVisibilityExtents(pDrawSkeleton->pSkeleton, vVisibleMin, vVisibleMax, true);

	PERFINFO_AUTO_START("Vis and shadows", 1);
	for (k = 0; k < gdraw->pass_count; ++k)
	{
		RdrDrawListPassData *pass = gdraw->passes[k];
		bool bDoOcclusion = gdraw->occlusion_buffer && pass == gdraw->visual_pass;
		int clip;
		Vec3 vEyePos;

		if ((draw_params.frustum_visible & pass->frustum_set_bit) == 0) {
			continue;
		}

		if (pDrawSkeleton->pSkeleton->bVisibilityChecked && pass == gdraw->visual_pass)
		{
			// don't test the visual frustum again
			if (!pDrawSkeleton->pSkeleton->bVisible)
			{
				draw_params.frustum_visible &= pass->frustum_clear_bits;
				gfxInvalidateLightCache(&pDrawSkeleton->pLightCache->base, LCIT_ALL);
				continue;
			}

			mulVecMat4(draw_params.root_mat[3], pass->viewmat, vEyePos);

			if (pDrawSkeleton->pSkeleton->bOcclusionChecked || pDrawSkeleton->pSkeleton->bAnimDisabled)
				bDoOcclusion = false;

			clip = pDrawSkeleton->pSkeleton->iVisibilityClipValue;
		}
		else
		{
			if (!(clip=frustumCheckBoundingBox(pass->frustum, vVisibleMin, vVisibleMax, draw_params.root_mat, true)))
			{
				draw_params.frustum_visible &= pass->frustum_clear_bits;
				gfxInvalidateLightCache(&pDrawSkeleton->pLightCache->base, LCIT_ALL);
				continue;
			}
		}

		if (bDoOcclusion)
		{
			Mat4 mViewSkel;
			bool bOcclusionChecked = false;
			mulMat4(pass->frustum->viewmat, draw_params.root_mat, mViewSkel);
			if (!zoTestBoundsSimpleWorld(gdraw->occlusion_buffer, vVisibleMin, vVisibleMax, mViewSkel, clip & FRUSTUM_CLIP_NEAR, &bOcclusionChecked))
			{
				draw_params.frustum_visible &= pass->frustum_clear_bits;
				pDrawSkeleton->pSkeleton->bOcclusionChecked = bOcclusionChecked;
				continue;
			}
			pDrawSkeleton->pSkeleton->bOcclusionChecked = bOcclusionChecked;
		}
	}

	if (!draw_params.frustum_visible)
	{
		PERFINFO_AUTO_STOP_CHECKED("Vis and shadows");
		PERFINFO_AUTO_PIX_STOP();
		return;
	}


	fDist = distance3(draw_params.root_mat[3], gdraw->cam_pos);

	// update frustum bounds per-skeleton
	{
		// Add margins
		mulBoundsAA(vVisibleMin, vVisibleMax, draw_params.root_mat, vWorldMin, vWorldMax);
		for (k = 0; k < gdraw->pass_count; ++k)
		{
			if (draw_params.frustum_visible & gdraw->passes[k]->frustum_set_bit)
				frustumUpdateBounds(gdraw->passes[k]->frustum, vWorldMin, vWorldMax);
		}
	}

	memcpy(&wl_params_backup, &draw_params.wl_params, sizeof(WLDynDrawParams));

	// Use ragdoll geometry to cast shadows for every skeleton except your own.
	// //-Don't use ragdoll for LOD 3 because that is the body sock, which is a single draw call anyway.
	// Using ragdoll anyway because when a character is outside the visible frustum, the
	//  skinning mats used for the bodysock do not get updated, but it gets set to the
	//  "bodysock" LOD or higher when it is offscreen.
	shadow_visible_bits = draw_params.frustum_visible & gdraw->shadow_frustum_bits;
	
	if (shadow_visible_bits)
		++dynDebugState.frameCounters.uiNumDrawnShadowPassSkels;

	if (shadow_visible_bits /*&& pDrawSkeleton->uiLODLevel < (worldLibGetLODSettings()->uiBodySockLODLevel+1) */&& !pDrawSkeleton->bIsLocalPlayer && !draw_params.bForceSkinnedShadows)
	{
		WLCostume* pCostume = GET_REF(pDrawSkeleton->pSkeleton->hCostume);
		U32 non_shadow_visible_bits = draw_params.frustum_visible & ~gdraw->shadow_frustum_bits;
		
		if (gfx_state.debug.drawRagdollShadowsForVisualPass)
		{
			non_shadow_visible_bits = 0;
			shadow_visible_bits = draw_params.frustum_visible;
		}

		// draw shadow casters
		if (pCostume && pDrawSkeleton->fTotalAlpha * pDrawSkeleton->fGeometryOnlyAlpha >= 1.f)
		{
			SkelInfo* pSkelInfo = GET_REF(pCostume->hSkelInfo);
			if (pSkelInfo)
			{
				DynRagdollData* pData = GET_REF(pSkelInfo->hRagdollData);
				if (pData)
				{
					draw_params.frustum_visible = shadow_visible_bits;

					FOR_EACH_IN_EARRAY(pData->eaShapes, DynRagdollShape, pShape)
					{
						const DynNode* pBone = dynSkeletonFindNode(pDrawSkeleton->pSkeleton, pShape->pcBone);
						if (pBone)
						{
							DynTransform shapeTransform;
							Quat qTemp;
							Vec3 vRotatedOffset;
							Mat4 mShapeMat;
							dynNodeGetWorldSpaceTransform(pBone, &shapeTransform);
							quatMultiplyInline(pShape->qRotation, shapeTransform.qRot, qTemp);
							copyQuat(qTemp, shapeTransform.qRot);



							switch (pShape->eShape)
							{
								xcase eRagdollShape_Box:
								{
									Vec3 vScaledOffset;
									mulVecVec3(pShape->vOffset, shapeTransform.vScale, vScaledOffset);
									quatRotateVec3Inline(shapeTransform.qRot, vScaledOffset, vRotatedOffset);
									addVec3(shapeTransform.vPos, vRotatedOffset, shapeTransform.vPos);

									dynTransformToMat4(&shapeTransform, mShapeMat);
									gfxQueueBox(pShape->vMin, pShape->vMax, mShapeMat, draw_params.frustum_visible, ROC_CHARACTER, RMATERIAL_BACKFACE, unitvec4, true);
								}

								xcase eRagdollShape_Capsule:
								{
									Vec3 vScale;
									F32 fLengthScale, fWidthScale;
									quatRotateVec3Inline(shapeTransform.qRot, pShape->vOffset, vRotatedOffset);
									addVec3(shapeTransform.vPos, vRotatedOffset, shapeTransform.vPos);
									quatRotateVec3Inline(pShape->qRotation, shapeTransform.vScale, vScale);

									fLengthScale = fabsf(vScale[1]);
									fWidthScale = MAX(fabsf(vScale[0]), fabsf(vScale[2]));

									unitVec3(shapeTransform.vScale);
									dynTransformToMat4(&shapeTransform, mShapeMat);
									gfxQueueCapsule(pShape->fHeightMin * fLengthScale, pShape->fHeightMax * fLengthScale, pShape->fRadius * fWidthScale, mShapeMat, draw_params.frustum_visible, ROC_CHARACTER, RMATERIAL_BACKFACE, unitvec4, true, 0);
								}
							}
						}
					}
					FOR_EACH_END;

					// reset frustum visible bits for visual passes
					// (Only turn off shadow bits for things that drew with the ragdoll data.)
					draw_params.frustum_visible = non_shadow_visible_bits;
				}
			}
		}
	}

	PERFINFO_AUTO_STOP_CHECKED("Vis and shadows");
	if (!draw_params.frustum_visible)
	{
		PERFINFO_AUTO_PIX_STOP();
		return;
	}

	PERFINFO_AUTO_START("Lights", 1);
	boxCalcMid(vWorldMin, vWorldMax, draw_params.lights_root);
	if (!draw_params.region_graphics_data)
	{
		WorldRegion *light_region = worldCellGetLightRegion(worldGetWorldRegionByPos(draw_params.lights_root));
		draw_params.region_graphics_data = worldRegionGetGraphicsData(light_region);
	}
	draw_params.lights_radius = pDrawSkeleton->pSkeleton->fStaticVisibilityRadius;

	if (draw_params.frustum_visible & gdraw->visual_frustum_bit)
	{
		Vec4_aligned eye_bounds[8];
		mulBounds(vWorldMin, vWorldMax, gdraw->visual_pass->viewmat, eye_bounds);
		gfxGetScreenSpace(gdraw->visual_pass->frustum, gdraw->nextVisProjection, 1, eye_bounds, &draw_params.fScreenArea);
		++dynDebugState.frameCounters.uiNumDrawnVisualPassSkels;

		// Hide FX Costumes if they are taking up the screen
		if (pDrawSkeleton->bFXCostume && draw_params.fScreenArea > 0.5f && !pDrawSkeleton->bIsLocalPlayer && !gdraw->hide_world)
		{
			fNearPlaneFadeOut = calcInterpParam(draw_params.fScreenArea, 0.75f, 0.5f);
			fNearPlaneFadeOut = CLAMP(fNearPlaneFadeOut, 0.5f, 1.0f);
		}

	}

	if (pDrawSkeleton->pLightCache && (draw_params.frustum_visible & gdraw->visual_frustum_bit))
	{
		if (pDrawSkeleton->bWorldLighting)
		{
			Mat4 mRoot;
			const DynSkeleton * pSkeleton = pDrawSkeleton->pSkeleton;
			dynNodeGetWorldSpaceMat(pSkeleton->pRoot, mRoot, false);
			gfxUpdateDynLightCachePosition(pDrawSkeleton->pLightCache, draw_params.region_graphics_data, 
				pSkeleton->vCurrentGroupExtentsMin, pSkeleton->vCurrentGroupExtentsMax, mRoot);
		}
		else
		{
			subVec3same(draw_params.lights_root, draw_params.lights_radius, vWorldMin);
			addVec3same(draw_params.lights_root, draw_params.lights_radius, vWorldMax);
			gfxUpdateDynLightCachePosition(pDrawSkeleton->pLightCache, 
				draw_params.region_graphics_data, 
				vWorldMin, vWorldMax, unitmat);
		}
	}
	PERFINFO_AUTO_STOP_START("Skinning mats", 1);

	// If we are skinning, we need to increment the ref count on the skinning mat array, and add it to a list do decrement once all drawing is done
	if (pDrawSkeleton->pCurrentSkinningMatSet)
	{
		dynSkinningMatSetIncrementRefCount(pDrawSkeleton->pCurrentSkinningMatSet);
		rdrDrawListClaimSkinningMatSet(gfx_state.currentAction->gdraw.draw_list, pDrawSkeleton->pCurrentSkinningMatSet);
	}
	PERFINFO_AUTO_STOP_START("Models", 1);

	uiNumGeos = (U32) eaSize(&pDrawSkeleton->eaDynGeos);
	for (uiGeoIndex=0; uiGeoIndex<uiNumGeos; ++uiGeoIndex)
	{
		DynDrawModel* pGeo = pDrawSkeleton->eaDynGeos[uiGeoIndex];
		U32 uiAdd;
		bool bMainNeedsZEquals=false;
		bool bQueuedBaseLayerAsAlpha=false;

		if (pGeo->bIsHidden)
			continue;
		if (pDrawSkeleton->uiLODLevel > pGeo->uiRequiredLOD)
			continue;
		if (gConf.bNewAnimationSystem && pDrawSkeleton->bBodySock != pGeo->bLOD)
			continue;

		// Restore backup
		memcpy(&draw_params.wl_params, &wl_params_backup, sizeof(WLDynDrawParams));
		dynDrawSkeletonGetFXDrivenDrawParams(pDrawSkeleton, &draw_params.wl_params, pGeo->pcOrigAttachmentBone, pGeo);
		draw_params.wl_params.alpha *= fNearPlaneFadeOut;

		// We need to draw every material add, and then either with the swap or if it doesn't exist then the original

		// Check for if the primary needs z-equals depth testing
		for (uiAdd=0; uiAdd < draw_params.wl_params.num_material_adds; ++uiAdd)
		{
			draw_params.wl_params.current_material_swap = &draw_params.wl_params.material_adds[uiAdd];
			if (draw_params.wl_params.current_material_swap->dissolve && !(draw_params.wl_params.current_material_swap->material_to_use->graphic_props.flags & (RMATERIAL_DECAL|RMATERIAL_DEPTHBIAS)))
			{
				bMainNeedsZEquals = true;
			}
		}

		// Draw either with swap or without (original material)
		draw_params.wl_params.current_material_swap = (draw_params.wl_params.material_swap.material_to_use || draw_params.wl_params.material_swap.do_texture_swap ||draw_params.wl_params.material_swap.use_fx_constants)?&draw_params.wl_params.material_swap:NULL;

		if (bMainNeedsZEquals)
			draw_params.additional_material_flags = RMATERIAL_DEPTH_EQUALS|RMATERIAL_NOZWRITE;


		if(!gfxShouldDrawModelAsCloth(pGeo, pDrawSkeleton))
		{
			gfxQueueDynModel(pDrawSkeleton, pGeo, fDist, &draw_params);
			if (gConf.bNewAnimationSystem && pGeo->pCloth)
				pGeo->pCloth->bQueueModelReset = 1;
		}
		else
		{
			if(pGeo->pCloth->iLastLOD != -1)
				gfxQueueDynClothMesh(&draw_params, pGeo->pCloth, pDrawSkeleton, pGeo);

			// LOD switches happen one frame behind, so the particles
			// have a chance to get initialized.
			pGeo->pCloth->iLastLOD = pGeo->pCloth->iCurrentLOD;
		}

		bQueuedBaseLayerAsAlpha = draw_params.queued_as_alpha;

		// First the adds
		for (uiAdd=0; uiAdd < draw_params.wl_params.num_material_adds; ++uiAdd)
		{
			draw_params.wl_params.current_material_swap = &draw_params.wl_params.material_adds[uiAdd];
			if (draw_params.wl_params.current_material_swap->dissolve && !(draw_params.wl_params.current_material_swap->material_to_use->graphic_props.flags & (RMATERIAL_DECAL|RMATERIAL_DEPTHBIAS)))
			{
				draw_params.additional_material_flags = RMATERIAL_NOCOLORWRITE;
			} else if (!(draw_params.wl_params.current_material_swap->material_to_use->graphic_props.flags & (RMATERIAL_DECAL|RMATERIAL_DEPTHBIAS)))
			{
				if (!(draw_params.wl_params.current_material_swap->material_to_use->graphic_props.flags & RMATERIAL_NOCOLORWRITE) && !bQueuedBaseLayerAsAlpha)
					draw_params.additional_material_flags = RMATERIAL_DEPTH_EQUALS;
			} else {
				if (!(draw_params.wl_params.current_material_swap->material_to_use->graphic_props.flags & RMATERIAL_NOCOLORWRITE))
					draw_params.additional_material_flags = RMATERIAL_DEPTHBIAS;
			}

			if (!pGeo->bCloth)
				gfxQueueDynModel(pDrawSkeleton, pGeo, fDist, &draw_params);
			else if (pGeo->pCloth)
				gfxQueueDynClothMesh(&draw_params, pGeo->pCloth, pDrawSkeleton, pGeo);

			draw_params.additional_material_flags = 0;
		}
		draw_params.queued_as_alpha = false;
	}

	PERFINFO_AUTO_STOP_START("Splats", 1);
	if (gdraw->do_splat_shadows && !pDrawSkeleton->bWorldLighting && uiNumGeos > 0)
	{
		if (SAFE_MEMBER(pDrawSkeleton->pSkeleton,ragdollState.bRagdollOn)) {
			U32 uiRagdollPart = 0;
			for (uiRagdollPart = 0; uiRagdollPart < pDrawSkeleton->pSkeleton->ragdollState.uiNumParts; uiRagdollPart++) {
				DynRagdollPartState *pRagdollPart = &pDrawSkeleton->pSkeleton->ragdollState.aParts[uiRagdollPart];
				if (!pRagdollPart->pSplat) {
					pRagdollPart->pSplat = gfxCreateRagdollPartShadowSplat(	pDrawSkeleton->pSkeleton,
																			pRagdollPart,
																			2.f);
				} else {
					gfxUpdateRagdollPartShadowSplat(pDrawSkeleton->pSkeleton,
													pRagdollPart,
													pRagdollPart->pSplat,
													pDrawSkeleton->pFxManager && pDrawSkeleton->pFxManager->bSplatsInvalid);
				}
				if (pRagdollPart->pSplat) {
					gfxQueueShadowSplat(pRagdollPart->pSplat,
										pDrawSkeleton->fTotalAlpha * pDrawSkeleton->fGeometryOnlyAlpha * 0.65f,
										draw_params.frustum_visible);
				}
			}
		} else {
			if (!pDrawSkeleton->pSplatShadow) {
				pDrawSkeleton->pSplatShadow = gfxCreateSkeletonShadowSplat(pDrawSkeleton->pSkeleton, 2.0f);
			} else {
				gfxUpdateSkeletonShadowSplat(	pDrawSkeleton->pSkeleton,
												pDrawSkeleton->pSplatShadow,
												pDrawSkeleton->pFxManager && pDrawSkeleton->pFxManager->bSplatsInvalid);
			}
			gfxQueueShadowSplat(pDrawSkeleton->pSplatShadow,
								pDrawSkeleton->fTotalAlpha * pDrawSkeleton->fGeometryOnlyAlpha * 0.65f,
								draw_params.frustum_visible);
		}
	}

	if (ao_do_splats && gdraw->do_ssao && uiNumGeos > 0)
	{
		if (!pDrawSkeleton->eaAOSplats) //we need to find the bones to make the splats
		{
			DynDrawSkeletonAOSplat* aoSplat;

			//Warning these are hardcoded which is bad, but this code is currently turned off by default 
			//- LDM
			aoSplat = calloc(1, sizeof(DynDrawSkeletonAOSplat));
			aoSplat->pBone = dynSkeletonFindNodeNonConst(pDrawSkeleton->pSkeleton, "Footr");
			if (aoSplat->pBone)
				eaPush(&pDrawSkeleton->eaAOSplats, aoSplat);
			else
				free(aoSplat);

			aoSplat = calloc(1, sizeof(DynDrawSkeletonAOSplat));
			aoSplat->pBone = dynSkeletonFindNodeNonConst(pDrawSkeleton->pSkeleton, "Footl");
			if (aoSplat->pBone)
				eaPush(&pDrawSkeleton->eaAOSplats, aoSplat);
			else
				free(aoSplat);

			aoSplat = calloc(1, sizeof(DynDrawSkeletonAOSplat));
			aoSplat->pBone = dynSkeletonFindNodeNonConst(pDrawSkeleton->pSkeleton, "Hips");
			aoSplat->splatType = 1;
			if (aoSplat->pBone)
				eaPush(&pDrawSkeleton->eaAOSplats, aoSplat);
			else
				free(aoSplat);
		}

		FOR_EACH_IN_EARRAY(pDrawSkeleton->eaAOSplats, DynDrawSkeletonAOSplat, curSplat)
		{
			Vec2 vSz;
			float alpha, maxamt;
			bool useOrientation;

			if (curSplat->splatType == 1)
			{
				setVec2(vSz, ao_hip_splat_sz_x, ao_hip_splat_sz_y);
				alpha = ao_hip_splat_amt;
				maxamt = ao_hip_splat_max;
				useOrientation = true;
			}
			else
			{
				setVec2(vSz, ao_foot_splat_sz, ao_foot_splat_sz);
				alpha = ao_foot_splat_amt;
				maxamt = ao_foot_splat_max;
				//we don't care about the orientation so we can skip it and avoid weirdness
				//during running when a foot is almost parallel the ground
				useOrientation = false; 
			}
			

			if (!curSplat->pAOSplat)
			{
				curSplat->pAOSplat = gfxCreateNodeShadowSplat(curSplat->pBone, vSz, 2.0f, useOrientation);
				gfxSplatSetMaterial(curSplat->pAOSplat, materialFind("Aosplat", WL_FOR_ENTITY));
			}
			else
			{
				gfxUpdateNodeShadowSplat(curSplat->pBone, curSplat->pAOSplat, vSz, useOrientation, false);
			}

			gfxQueueAOShadowSplat(curSplat->pAOSplat, pDrawSkeleton->fTotalAlpha * pDrawSkeleton->fGeometryOnlyAlpha * alpha, maxamt, draw_params.frustum_visible);
		}
		FOR_EACH_END
	}
	PERFINFO_AUTO_STOP_CHECKED("Splats");

	PERFINFO_AUTO_PIX_STOP();
}

static int addModelMemory(void *modelMemory, StashElement elem)
{
	Model *model = stashElementGetKey(elem);
	ModelLOD *model_lod = modelGetLOD(model, 0);
	*((U32 *)modelMemory) += modelLODGetBytesTotal(model_lod);
	return 1;
}

static int addMaterialMemory(void *materialMemory, StashElement elem)
{
	U32 total, shared;
	Material *material = stashElementGetKey(elem);
	gfxMaterialsGetMemoryUsage(material, &total, &shared);
	*((U32 *)materialMemory) += total - shared;
	return 1;
}

void gfxGetSkeletonMemoryUsage(DynDrawSkeleton* pDrawSkeleton, U32* modelMemory, U32* materialMemory)
{
	StashTable modelHash = stashTableCreateAddress(128);
	StashTable materialHash = stashTableCreateAddress(128);
	U32 uiNumGeos = (U32) eaSize(&pDrawSkeleton->eaDynGeos);
	U32 uiGeoIndex;

	*modelMemory = 0;
	*materialMemory = 0;

	for (uiGeoIndex=0; uiGeoIndex<uiNumGeos; ++uiGeoIndex)
	{
		DynDrawModel* pGeo = pDrawSkeleton->eaDynGeos[uiGeoIndex];
		int i;
		stashAddressAddPointer(modelHash, pGeo->pModel, pGeo->pModel, false);
		// TODO apply material and texture swaps
		for (i=0; i<2; ++i)
			if (pGeo->pMaterial[i])
				stashAddressAddPointer(materialHash, pGeo->pMaterial[i], pGeo->pMaterial[i], false);
	}

	stashForEachElementEx(modelHash, addModelMemory, modelMemory);
	stashForEachElementEx(materialHash, addMaterialMemory, materialMemory);

	stashTableDestroy(modelHash);
	stashTableDestroy(materialHash);
}

void gfxQueueSingleDynDrawSkeleton(DynDrawSkeleton* pDrawSkeleton, WorldRegion* pOverrideRegion, bool bDrawParticlesToo, bool bForceSkinnedShadows, float* overrideModColor)
{
	GfxGlobalDrawParams *gdraw = gfx_state.currentAction ? &gfx_state.currentAction->gdraw : NULL;
	DynDrawParams draw_params = {0};

	if (gfx_state.currentDevice->skipThisFrame || !gdraw)
		return;

	if( overrideModColor ) {
		copyVec3(overrideModColor, draw_params.wl_params.color);
		draw_params.wl_params.mod_color_on_all_costume_colors = true;
	} else {
		copyVec3(onevec3, draw_params.wl_params.color);
	}
	copyVec3(onevec3, draw_params.wl_params.ambient_multiplier);
	copyVec3(zerovec3, draw_params.wl_params.ambient_offset);
	draw_params.frustum_visible = gdraw->all_frustum_bits;
	draw_params.noMaterialPreSwap = true;
	draw_params.bForceSkinnedShadows = bForceSkinnedShadows;

	draw_params.region_graphics_data = pOverrideRegion ? worldRegionGetGraphicsData(pOverrideRegion) : NULL;

	gfxQueueSkeleton(pDrawSkeleton, &draw_params);

	// Now draw every particle
	if (bDrawParticlesToo && gdraw->visual_pass)
	{
		if(pOverrideRegion) {
			dynParticleForEachInFrustum(gfxQueueDynParticle, gfxQueueFastParticle, pOverrideRegion, &draw_params.wl_params, gdraw->visual_pass->frustum, NULL, NULL);
		} else {
			FOR_EACH_IN_EARRAY_FORWARDS(gdraw->regions, WorldRegion, pRegion)
				dynParticleForEachInFrustum(gfxQueueDynParticle, gfxQueueFastParticle, pRegion, &draw_params.wl_params, gdraw->visual_pass->frustum, NULL, NULL);
			FOR_EACH_END;
		}
	}
}

void gfxClearAllFxGrids(void)
{
	GfxGlobalDrawParams *gdraw = gfx_state.currentAction ? &gfx_state.currentAction->gdraw : NULL;
	if (!gdraw)
		return;
	FOR_EACH_IN_EARRAY_FORWARDS(gdraw->regions, WorldRegion, pRegion)
	{
		worldRegionClearGrid(pRegion);
	}
	FOR_EACH_END;
}

void gfxDynamicsQueueAllWorld(void)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	U32 uiNumDrawSkeletons = (U32)eaSize(&eaDrawSkelList);
	U32 uiDrawSkeletonIndex;
	DynDrawParams draw_params = {0};

	etlAddEvent(gfx_state.currentDevice->event_timer, "Queue dynamics", ELT_CODE, ELTT_BEGIN);
	PERFINFO_AUTO_START_FUNC_PIX();

	ZeroStructForce(&dynDebugState.frameCounters);
	dynDrawTrackersClear();

	draw_params.wl_params.alpha = 1.0f;
	copyVec3(onevec3, draw_params.wl_params.color);
	copyVec3(onevec3, draw_params.wl_params.ambient_multiplier);
	copyVec3(zerovec3, draw_params.wl_params.ambient_offset);
	draw_params.frustum_visible = gdraw->all_frustum_bits;

	// First, grab every draw skeleton and draw those
	if (!gdraw->hide_world)
	{

		PERFINFO_AUTO_START_PIX("gfxQueueSkeleton", uiNumDrawSkeletons);
		for (uiDrawSkeletonIndex=0; uiDrawSkeletonIndex<uiNumDrawSkeletons; ++uiDrawSkeletonIndex)
		{
			if (eaDrawSkelList[uiDrawSkeletonIndex]->bInvalid)
				continue;
			draw_params.region_graphics_data = NULL;
			if (eaDrawSkelList[uiDrawSkeletonIndex]->bIsLocalPlayer)
				gfxEnsureAssetsLoadedForSkeleton(eaDrawSkelList[uiDrawSkeletonIndex]);
			draw_params.bForceSkinnedShadows = eaDrawSkelList[uiDrawSkeletonIndex]->bWorldLighting; // Interaction objects, etc
			gfxQueueSkeleton(eaDrawSkelList[uiDrawSkeletonIndex], &draw_params);
		}
		PERFINFO_AUTO_STOP_PIX();
		draw_params.bForceSkinnedShadows = 0;

		// Now draw every particle
		// TODO DJR add the per-frustum test for geometry particles
		draw_params.frustum_visible = gdraw->visual_frustum_bit;
		if (gdraw->visual_pass)
		{
			PERFINFO_AUTO_START_PIX("Queue Region Particles", 1);

			// the first region should be the most important
			FOR_EACH_IN_EARRAY_FORWARDS(gdraw->regions, WorldRegion, pRegion)
			{
				gfxSetCurrentRegion(FOR_EACH_IDX(gdraw->regions, pRegion));
				dynParticleForEachInFrustum(gfxQueueDynParticle, gfxQueueFastParticle, pRegion, &draw_params.wl_params, gdraw->visual_pass->frustum, zoTestBoundsSimple, gdraw->occlusion_buffer);
			}
			FOR_EACH_END;
			PERFINFO_AUTO_STOP_PIX();

			PERFINFO_AUTO_START_PIX("Queue UI Particles", 1);
			gfxSetCurrentRegion(0);
			if (dynFxGetUiManager(true))
				dynFxManForEachParticle(dynFxGetUiManager(true), gfxQueueDynParticle, gfxQueueFastParticle, &draw_params);
			PERFINFO_AUTO_STOP_PIX();
		}
	}
	else
	{
		draw_params.frustum_visible = gdraw->visual_frustum_bit;
	}



	PERFINFO_AUTO_START_PIX("Queue Editor Particles", 1);
	draw_params.bIsInFPEditor = true;
	FOR_EACH_IN_EARRAY(eaEditorFastParticleSets, DynFxFastParticleSet, pSet)
		gfxQueueFastParticle(pSet, &draw_params);
	FOR_EACH_END
	PERFINFO_AUTO_STOP_PIX();
	eaDestroy(&eaEditorFastParticleSets);

	dynDrawTrackersPostProcess();

	if (dynDebugState.bRecordFXProfile)
		fxRecording.uiNumDynFxDrawn += dynDebugState.frameCounters.uiNumDrawnFx;


	// These should be rare enough that we don't need to frustum cull them.

	PERFINFO_AUTO_STOP_FUNC_PIX();
	etlAddEvent(gfx_state.currentDevice->event_timer, "Queue dynamics", ELT_CODE, ELTT_END);
}


void gfxDynamicsQueueAllUI(RdrDrawList* drawList)
{
	GfxGlobalDrawParams *gdraw = &gfx_state.currentAction->gdraw;
	DynFxManager* uiManager = dynFxGetUiManager(false);
	DynDrawParams draw_params = { 0 };
	RdrDrawList *oldDrawList;

	if (!uiManager)
		return;

	PERFINFO_AUTO_START_FUNC();

    // Particle sets
	draw_params.wl_params.is_screen_space = true;
	oldDrawList = gdraw->draw_list;
	gdraw->draw_list = drawList;
	draw_params.frustum_visible = 0xFFFFFFFF;
	dynFxManForEachParticle(uiManager, gfxQueueDynParticle, gfxQueueFastParticle, &draw_params);
	FOR_EACH_IN_EARRAY(eaEditorFastParticleSets2d, DynFxFastParticleSet, pSet)
		gfxQueueFastParticle(pSet, &draw_params);
	FOR_EACH_END
	eaDestroy(&eaEditorFastParticleSets2d);
	gdraw->draw_list = oldDrawList;

	PERFINFO_AUTO_STOP();
}

void gfxQueueFastParticleFromEditor(DynFxFastParticleSet* pSet, bool bScreenSpace)
{
	eaPush( bScreenSpace? &eaEditorFastParticleSets2d : &eaEditorFastParticleSets, pSet);
}

void gfxQueueClearEditorSets(void)
{
	eaDestroy(&eaEditorFastParticleSets);
	eaDestroy(&eaEditorFastParticleSets2d);
}


bool gfxSkeletonIsVisible( DynSkeleton* pSkeleton )
{
	GfxGlobalDrawParams *gdraw = SAFE_MEMBER_ADDR(gfx_state.currentAction, gdraw);
	//Vec3 vRootPos, vEyePos;
	Mat4 mSkel, mViewSkel;
	Vec3 vMin, vMax;

	PERFINFO_AUTO_START_FUNC();

	//dynNodeGetWorldSpacePos(pSkeleton->pRoot, vRootPos);

	if ( !gfx_state.currentCameraView || !gfx_state.currentVisFrustum )
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	//mulVecMat4(vRootPos, gfx_state.currentVisFrustum->viewmat, vEyePos);
	dynNodeGetWorldSpaceMat(pSkeleton->pRoot, mSkel, false);

	dynSkeletonGetVisibilityExtents(pSkeleton, vMin, vMax, true);

	if (!(pSkeleton->iVisibilityClipValue=frustumCheckBoundingBox(gfx_state.currentVisFrustum, vMin, vMax, mSkel, true)))
	{
		PERFINFO_AUTO_STOP_FUNC();
		return false;
	}

	mulMat4(gfx_state.currentVisFrustum->viewmat, mSkel, mViewSkel);

	{
		bool bOcclusionChecked = false;
		if (gfx_state.currentCameraView && gfx_state.currentCameraView->occlusion_buffer && !pSkeleton->bAnimDisabled && !gfx_state.debug.noZocclusion && !zoTestBoundsSimpleWorld(gfx_state.currentCameraView->occlusion_buffer, vMin, vMax, mViewSkel, pSkeleton->iVisibilityClipValue & FRUSTUM_CLIP_NEAR, &bOcclusionChecked))
		{
			PERFINFO_AUTO_STOP_FUNC();
			pSkeleton->bOcclusionChecked = bOcclusionChecked;
			return false;
		}
		pSkeleton->bOcclusionChecked = bOcclusionChecked;
	}

	PERFINFO_AUTO_STOP_FUNC();
	return true;
}

static WorldRegion **visible_regions;
static DynFxRegion **visible_fx_regions;
static F32 *region_camera_positions;
static F32 *region_eye_space_offsets;
static int *region_hidden_object_ids;
static U32 region_last_update_timestamp;

bool gfxParticleIsVisible( DynParticle* pParticle, DynFxRegion* pFxRegion )
{
	Vec3 vRootPos, vEyePos;
	int i;

	dynNodeGetWorldSpacePos(&pParticle->pDraw->node, vRootPos);

	if ( !gfx_state.currentCameraView || !gfx_state.currentVisFrustum )
	{
		return false;
	}

	if (gfx_state.client_frame_timestamp != region_last_update_timestamp)
	{
		region_last_update_timestamp = gfx_state.client_frame_timestamp;
		eaSetSize(&visible_regions, 0);
		eaSetSize(&visible_fx_regions, 0);
		eafSetSize(&region_camera_positions, 0);
		eafSetSize(&region_eye_space_offsets, 0);
		eaiSetSize(&region_hidden_object_ids, 0);

		eaPush(&visible_regions, worldGetWorldRegionByPos(gfx_state.currentCameraFocus));
		eaPush(&visible_fx_regions, worldRegionGetFXRegion(visible_regions[0]));
		eafPush3(&region_camera_positions, gfx_state.currentVisFrustum->cammat[3]);
		eafPush3(&region_eye_space_offsets, zerovec3);
		eaiPush(&region_hidden_object_ids, 0);

		worldRegionGetAssociatedRegionList(&visible_regions, &region_camera_positions, &region_hidden_object_ids);

		if (eaSize(&visible_regions) > 1)
		{
			eafSetSize(&region_eye_space_offsets, eafSize(&region_camera_positions));
			eaSetSize(&visible_fx_regions, eaSize(&visible_regions));
			for (i = 1; i < eaSize(&visible_regions); ++i)
			{
				Vec3 vWorldOffset;
				subVec3(region_camera_positions, &region_camera_positions[i*3], vWorldOffset);
				mulVecMat3(vWorldOffset, gfx_state.currentVisFrustum->viewmat, &region_eye_space_offsets[i*3]);
				visible_fx_regions[i] = worldRegionGetFXRegion(visible_regions[i]);
			}
		}
	}

	mulVecMat4(vRootPos, gfx_state.currentVisFrustum->viewmat, vEyePos);

	for (i = 1; i < eaSize(&visible_regions); ++i)
	{
		if (visible_fx_regions[i] == pFxRegion)
			addVec3(vEyePos, &region_eye_space_offsets[i*3], vEyePos);
	}

	if (!(pParticle->iVisibilityClipValue=frustumCheckSphere(gfx_state.currentVisFrustum, vEyePos, pParticle->fVisibilityRadius)))
	{
		return false;
	}

 	if (gfx_state.currentCameraView && gfx_state.currentCameraView->occlusion_buffer && !gfx_state.debug.noZocclusion && !zoTestSphere(gfx_state.currentCameraView->occlusion_buffer, vEyePos, pParticle->fVisibilityRadius, pParticle->iVisibilityClipValue & FRUSTUM_CLIP_NEAR, &pParticle->bOcclusionChecked))
 	{
 		return false;
 	}
	return true;
}

void gfxDynFxUpdateLight(DynParticle *pParticle, GfxLight **ppLight)
{
	DynLight* pDynLight = pParticle ? pParticle->pDraw->pDynLight : NULL;
	LightData ld = {0};

	if (!pDynLight || !pParticle || 
		pDynLight->eLightType == WL_LIGHT_NONE || 
		!gfx_state.settings.dynamicLights)
	{
		if ( *ppLight )
		{
			gfxRemoveLight(*ppLight);
			*ppLight = NULL;
		}
		return;
	}

	// Ok, we have a light, copy over the data
	// OPTIMIZE ME: We probably should only update data when it changes

	dynParticleCopyToDynLight(pParticle, pDynLight);
	pDynLight->vDiffuseHSV[2] *= pParticle->fFadeOut;
	pDynLight->vSpecularHSV[2] *= pParticle->fFadeOut;

	// this check must happen after the call to dynParticleCopyToDynLight so the radius is set
	if (pDynLight->fRadius <= 0 || pParticle->fFadeOut <= 0.0f)
	{
		if ( *ppLight )
		{
			gfxRemoveLight(*ppLight);
			*ppLight = NULL;
		}
		return;
	}


	// Every light type needs these guys:
	ld.light_type = pDynLight->eLightType;
	ld.cast_shadows = !!pDynLight->bCastShadows;
	ld.dynamic = 1;
	copyVec3(pDynLight->vDiffuseHSV, ld.diffuse_hsv);
	copyVec3(pDynLight->vSpecularHSV, ld.specular_hsv);

	if (pParticle->pDraw->fHueShift)
	{
		ld.diffuse_hsv[0] += pParticle->pDraw->fHueShift;
		hsvMakeLegal(ld.diffuse_hsv, false);
		ld.specular_hsv[0] += pParticle->pDraw->fHueShift;
		hsvMakeLegal(ld.specular_hsv, false);
	}

	// world matrix
	{
		Quat qRot, qResult;
		Quat qFix = {-0.7071067811f, 0.0f, 0.0f, -0.7071067811f};
		dynNodeGetWorldSpaceRot(&pParticle->pDraw->node, qRot);
		quatMultiply(qFix, qRot, qResult);

		quatToMat(qResult, ld.world_mat);
	}
	dynNodeGetWorldSpacePos(&pParticle->pDraw->node, ld.world_mat[3]);

	switch( pDynLight->eLightType )
	{
		xcase WL_LIGHT_PROJECTOR:
		{
			// Cone
			ld.inner_cone_angle2 = pDynLight->fInnerConeAngle;
			ld.outer_cone_angle2 = pDynLight->fOuterConeAngle;

			// Projected texture name
			ld.texture_name = pParticle->pDraw->pcTextureName;
		}
		case WL_LIGHT_SPOT: // Fall through, since a projector light has everything the spot light has
		{
			// Cone
			ld.inner_cone_angle = pDynLight->fInnerConeAngle;
			ld.outer_cone_angle = pDynLight->fOuterConeAngle;

		}
		case WL_LIGHT_POINT: // Fall through, since a spot light has everything the point light has
		{
			// Radius
			ld.outer_radius = pDynLight->fRadius;
			ld.inner_radius = pDynLight->fInnerRadiusPercentage * ld.outer_radius;
		}
		xdefault:
		case WL_LIGHT_NONE:
		{
			// do nothing
			return;
		}
	};
		
	// Update the light
	*ppLight = gfxUpdateLight(*ppLight, &ld, 0, NULL);
}




void gfxDynamicsStartup(void)
{
	dynFxSetUpdateLightFunc(gfxDynFxUpdateLight);
}


int gfxDynamicsGetParticleMemUsage(DynParticle* pPart)
{
	int iTotal = 0;
	if (pPart->pDraw->pModel)
		iTotal += modelLODGetBytesTotal(modelLoadLOD(pPart->pDraw->pModel, 0));
	if (pPart->pDraw->pMaterial)
	{
		U32 uiTotalMem, uiSharedMem;
		gfxMaterialsGetMemoryUsage(pPart->pDraw->pMaterial, &uiTotalMem, &uiSharedMem);
		iTotal += uiTotalMem;
	}
	if (pPart->pDraw->pTexture && pPart->pDraw->pTexture->loaded_data)
	{
		iTotal += pPart->pDraw->pTexture->loaded_data->tex_memory_use[TEX_MEM_VIDEO] + pPart->pDraw->pTexture->loaded_data->tex_memory_use[TEX_MEM_LOADING];
	}
	return iTotal;
}

void gfxDynDrawModelClearAllBodysocks(void)
{
	dynDrawClearCostumeBodysocks();
}
