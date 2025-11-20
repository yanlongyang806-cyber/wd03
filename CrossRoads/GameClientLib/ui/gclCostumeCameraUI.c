/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

#include "CostumeCommon.h"
#include "CostumeCommonLoad.h"
#include "EditorManager.h"
#include "GameClientLib.h"
#include "gclBaseStates.h"
#include "gclCamera.h"
#include "gclCostumeUI.h"
#include "gclCostumeUIState.h"
#include "gclCostumeView.h"
#include "gfxCamera.h"
#include "GlobalStateMachine.h"
#include "inputGamepad.h"
#include "inputMouse.h"
#include "StringCache.h"
#include "UIGen.h"
#include "WorldGrid.h"

#include "AutoGen/gclCostumeCameraUI_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

// --------------------------------------------------------------------------
// Data Definitions
// --------------------------------------------------------------------------

CostumeViewGraphics *g_pCostumeView = NULL;

AUTO_STRUCT;
typedef struct BackgroundPlayerCostume
{
	const char *pcName; AST(POOL_STRING KEY)
	REF_TO(PlayerCostume) hCostume;
	const char *pcSlotType; AST(POOL_STRING)
	REF_TO(PCMood) hMood;
	REF_TO(CharacterClass) hClass;
	bool bDirty;
} BackgroundPlayerCostume;

static BackgroundPlayerCostume **s_eaBackgroundCostumes;

static GfxCameraController s_Camera;
static bool s_bCreatorHidesWorld = true;

static bool s_bRecalcHeight = false;
static F32 s_fFixedHeightMod = 0.0;

static F32 s_fRotMult = 1.0f;
static F32 s_fZoomMult = 1.0f;
static F32 s_fYawSpeed = 0;
static F32 s_fPitchSpeed = 0;
static F32 s_fShipYawSpeed = 0;
static F32 s_fShipPitchSpeed = 0;
static F32 s_fShipRollSpeed = 0;
static F32 s_fZoomSpeed = 0;

static F32 s_fZoomStep = 0;

static Vec3 s_v3OldCamPos;
static Vec3 vDefaultCharPos = {0, 0, 0};

static bool s_bMouseRotateFlag = 0;
static bool s_bMouseRotate3DFlag = 0;

static bool s_bHideCostume = false;
static int s_iSuppressCostume = 0;
static bool s_bShowCostumeItems = false;
static bool s_bPointCameraAtCostume = false;

// Interpolate toward these values each frame in the CamFunc
Vec3 s_vInterpCamCenter = {0.0f, 0.0f, 0.0f};
Vec3 s_vInterpCamPos = {0.0f, 0.0f, 0.0f};
F32 s_fInterpCamSeconds = 0.0f;

// In radians per second
#define ROTATION_SPEED PI
// In units per second
#define ZOOM_SPEED 20

#define IS_SANE_FOV(x)		((x) >= 1.0 && (x) <= 179.0)
#define IS_SANE_SCALE(x)	((x) >= 0.1 && (x) <= 10.0)

void costumeCameraUI_CamViewReset(void);
void costumeCameraUI_RemoveBackgroundCostume(const char *pchCostumeName);

// --------------------------------------------------------------------------
// Utility Functions
// --------------------------------------------------------------------------

static bool costumeCameraUI_ShouldAutoAdjustTailorCamDist(PlayerCostume *pPlayerCostume)
{
	PCSkeletonDef *pSkel = GET_REF(pPlayerCostume->hSkeleton);
	if (pSkel) {
		return pSkel->bAutoAdjustTailorCamDistance;
	}
	return false;
}


static float costumeCameraUI_GetTailorCamMaxDist(PlayerCostume *pPlayerCostume)
{
	PCSkeletonDef *pSkel = GET_REF(pPlayerCostume->hSkeleton);
	if (pSkel) {
		return pSkel->fMaxTailorCamDist;
	}
	return 5.0f;
}


static float costumeCameraUI_GetTailorCamMinDist(PlayerCostume *pPlayerCostume)
{
	PCSkeletonDef *pSkel = GET_REF(pPlayerCostume->hSkeleton);
	if (pSkel) {
		return pSkel->fMinTailorCamDist;
	}
	return 5.0f;
}

void costumeCameraUI_ClearBackgroundCostumes(void)
{
	while (eaSize(&s_eaBackgroundCostumes) > 0) {
		costumeCameraUI_RemoveBackgroundCostume(s_eaBackgroundCostumes[0]->pcName);
	}
}

void costumeCameraUI_FitCostumeToBox(SA_PARAM_NN_VALID CBox *pBox, bool bScaleToHeight, F32 fOriginX, F32 fOriginY, F32 fZoom, PlayerCostume *pPlayerCostume, const char *pcSelectedBone, const char *pcCenterBone, bool bBothMode, PCStanceInfo *pStance, CostumeViewGraphics *pCameraView)
{
	PCSkeletonDef *pSkel;
	F32 fBaseHeight, fHeight, fWidth;
	F32 fHeightMod = 0, fWidthMod = 0, fDepthMod = 0;
	const DynNode *pSelectedNode;
	const DynNode *pCenterNode;
	Vec3 vMin, vMax, vLen;
	F32 fVisibleHeight, fVisibleRadius;

	if (!pPlayerCostume || !GET_REF(pPlayerCostume->hSkeleton) || !pStance || !pCameraView || !pCameraView->costume.pSkel)
		return;

	pSkel = GET_REF(pPlayerCostume->hSkeleton);
	pSelectedNode = dynSkeletonFindNode(pCameraView->costume.pSkel, pcSelectedBone);
	pCenterNode = dynSkeletonFindNode(pCameraView->costume.pSkel, pcCenterBone);

	// Determine base height
	fHeight = fBaseHeight = bScaleToHeight ? pPlayerCostume->fHeight : pSkel->fHeightMax;

	// Size calculations based off visibility extents
	dynSkeletonGetVisibilityExtents(pCameraView->costume.pSkel, vMin, vMax);
	subVec3(vMin, vMax, vLen);

	fVisibleHeight = vecY(vMax) - vecY(vMin);
	fWidth = fVisibleRadius = sqrtf(SQR(vecX(vMax) - vecX(vMin)) + SQR(vecZ(vMax) - vecZ(vMin))) * 0.5f;

	if (fHeight <= 0)
		fHeight = fVisibleHeight;

	// If there's enough info, 
	if (pSelectedNode && pCenterNode)
	{
		Vec3 vRoot, vSelected, vCenter;
		Vec3 vDepth, vDistance;

		dynNodeGetWorldSpacePos(pCameraView->costume.pSkel->pRoot, vRoot);
		dynNodeGetWorldSpacePos(pSelectedNode, vSelected);
		dynNodeGetWorldSpacePos(pCenterNode, vCenter);

		subVec3(vSelected, vRoot, vDistance);
		subVec3(vSelected, vCenter, vDepth);

		fHeightMod = vecY(vDepth); // Set the HeightMod to the node Y position.
		MAX1F(fHeightMod, 0);

		// If looking at a single bone...
		// Set the WidthMod to the distance away from the center of the 
		if (!bBothMode)
		{
			GfxCameraController *pCamera = costumeView_GetCamera();
			// Project the X, Z of the distance onto the camera's viewing plane.
			Vec2 v2Point = { vDistance[0], vDistance[2] };
			Vec2 v2Plane = { -pCamera->centeroffset[2], pCamera->centeroffset[0] };
			fWidthMod = dotVec2(v2Point, v2Plane) / lengthVec2(v2Plane);
		}

		fDepthMod = lengthVec3(vDepth) + 0.5f * fVisibleHeight;
	}
	else
	{
		// Auto-calculate based on pure visible bounding box
		fHeightMod = fVisibleHeight * 0.5f; // center vertically
		fDepthMod = lengthVec3(vLen) * 0.5f; // use half the larger BBOX dimension
	}

	costumeView_CalculateCamera(pCameraView, pBox, fOriginX, fOriginY, fWidth, fHeight, fWidthMod, fHeightMod, fDepthMod, fZoom);
}

void costumeCameraUI_FitCostumeToGenInternal(SA_PARAM_NN_VALID UIGen *pGen, bool bScaleToHeight, F32 fOriginX, F32 fOriginY, F32 fZoom, PlayerCostume *pPlayerCostume, const char *pcSelectedBone, const char *pcCenterBone, bool bBothMode, PCStanceInfo *pStance, CostumeViewGraphics *pCameraView)
{
	costumeCameraUI_FitCostumeToBox(&pGen->ScreenBox, bScaleToHeight, fOriginX, fOriginY, fZoom, pPlayerCostume, pcSelectedBone, pcCenterBone, bBothMode, pStance, pCameraView);
}


void costumeCameraUI_FitCameraToBox(SA_PARAM_NN_VALID CBox *pBox, bool bScaleToHeight, bool bZoom, bool bAllowMouseWheel, PlayerCostume *pPlayerCostume, PCStanceInfo *pStance, CostumeViewGraphics *pCameraView)
{
	static const char *s_pcLastStance = NULL;
	static F32 s_fHeightMod = 0.0f;
	static F32 s_fDepthMod = 0.0f;
	static U32 s_uiNextHeightChange = 0;
	static F32 s_fCostumeHeight = 10;
	static F32 s_fZoomSmooth = 0.0f;
	static F32 s_fLastOverallScale = 0.0f;
	static bool s_bCheckForOverall = false;
	static const char *s_pcPooled_Overall = NULL;

	F32	fHeight, fCH, fZoom, fZoomMax = -1.0f;
	F32 fOverallScale = 0.0f;
	bool bUseHeightOffset = false;
	int i;

	if ( pPlayerCostume==NULL || pCameraView==NULL || pCameraView->costume.pSkel==NULL ) 
		return;

	if (!s_bCheckForOverall)
	{
		s_pcPooled_Overall = allocFindString("Overall");
		s_bCheckForOverall = true;
	}

	if (s_pcPooled_Overall)
	{
		WLCostume *pCostume = GET_REF(pCameraView->costume.hWLCostume);
		if (pCostume)
		{
			for (i = 0; i < eaSize(&pCostume->eaScaleValue); i++)
			{
				if (pCostume->eaScaleValue[i]->pcScaleGroup == s_pcPooled_Overall)
				{
					fOverallScale = pCostume->eaScaleValue[i]->vScaleInputs[0];
					break;
				}
			}
		}
	}

	fHeight =	( bScaleToHeight ) ? pPlayerCostume->fHeight : 7.0f;
	fCH =		( pPlayerCostume->fHeight );
	fZoom =		( bAllowMouseWheel && (mouseClickHit(MS_WHEELUP, pBox) || mouseClickHit(MS_WHEELDOWN, pBox))) ? -(F32)mouseZ() : 0;

	if (s_fZoomStep)
	{
		fZoom += s_fZoomStep;
		s_fZoomStep = 0;
	}

	fZoom *= s_fZoomMult;

	if (fZoom)
	{
		s_fZoomSmooth += fZoom;
	}

	if (s_fZoomSmooth)
	{
		F32 fTimeScale = s_fZoomSpeed ? ZOOM_SPEED * s_fZoomSpeed : 5;
		F32 fZoomSmoothing = MIN(s_fZoomSmooth, g_ui_State.timestep * fTimeScale);
		if (s_fZoomSmooth < 0)
		{
			fZoomSmoothing = MAX(s_fZoomSmooth, -g_ui_State.timestep * fTimeScale);
		}
		s_fZoomSmooth -= fZoomSmoothing;
		fZoom = fZoomSmoothing;
	}

	if (!costumeCameraUI_ShouldAutoAdjustTailorCamDist(pPlayerCostume) && !s_fFixedHeightMod)
	{
		if ( s_bRecalcHeight || (s_pcLastStance != (pStance ? pStance->pcName : NULL)) || s_fCostumeHeight != fCH || s_fLastOverallScale != fOverallScale )
		{
			s_pcLastStance = pStance ? pStance->pcName : NULL;

			s_uiNextHeightChange = gGCLState.totalElapsedTimeMs + 250;

			s_fCostumeHeight = fCH;
			s_fLastOverallScale = fOverallScale;
			s_bRecalcHeight = false;
		}
		else if ( s_uiNextHeightChange > 0 && s_uiNextHeightChange < gGCLState.totalElapsedTimeMs )
		{
			const DynNode* pHead = dynSkeletonFindNode( pCameraView->costume.pSkel, "Head" );
			const DynNode* pHips = dynSkeletonFindNode( pCameraView->costume.pSkel, "Hips" );

			Vec3 vRoot, vHead, vHips, vDepth;

			if ( !pHead || !pHips ) return;

			dynNodeGetWorldSpacePos( pCameraView->costume.pSkel->pRoot, vRoot );
			dynNodeGetWorldSpacePos( pHead, vHead );
			dynNodeGetWorldSpacePos( pHips, vHips );

			subVec3(vHead, vRoot, vHead);
			subVec3(vHips, vRoot, vHips);

			s_fHeightMod = vHead[1] - fabs(fCH-vHead[1])*0.2f - fCH * 0.15f;

			subVec3( vHead, vHips, vDepth );

			s_fDepthMod = sqrt( vDepth[0] * vDepth[0] + vDepth[2] * vDepth[2] ) + fCH * 0.5f;

			s_uiNextHeightChange = -1;
		}
	}
	else
	{
		if (costumeCameraUI_ShouldAutoAdjustTailorCamDist(pPlayerCostume))
		{
			Vec3 vMin, vMax, vLen;
			F32 fZoomMin;
			dynSkeletonGetVisibilityExtents(pCameraView->costume.pSkel, vMin, vMax);
			subVec3(vMin, vMax, vLen);
			scaleByVec3(vLen, 0.5f);
			fZoomMin = lengthVec3(vLen);
			s_fDepthMod = fZoomMin*MAXF(costumeCameraUI_GetTailorCamMinDist(pPlayerCostume),1.0f)+1.0f;
			fZoomMax = fZoomMin*MAXF(costumeCameraUI_GetTailorCamMaxDist(pPlayerCostume),1.1f)+1.0f;
			bUseHeightOffset = true;
		}
		else
		{
			s_fDepthMod = costumeCameraUI_GetTailorCamMinDist(pPlayerCostume);
			fZoomMax = costumeCameraUI_GetTailorCamMaxDist(pPlayerCostume);
		}
		if (!bUseHeightOffset && s_fFixedHeightMod)
		{
			s_fHeightMod = s_fFixedHeightMod;
		}
		else
		{
			s_fHeightMod = 0.0f;
		}
	}

	costumeView_UpdateCamera(pCameraView, pBox, fHeight, s_fHeightMod, s_fDepthMod, bZoom, fZoom, fZoomMax, bUseHeightOffset, &s_fZoomSmooth);
}


void costumeCameraUI_FitCameraToGenInternal(SA_PARAM_NN_VALID UIGen *pGen, bool bScaleToHeight, bool bZoom, bool bAllowMouseWheel, PlayerCostume *pPlayerCostume, PCStanceInfo *pStance, CostumeViewGraphics *pCameraView)
{
	costumeCameraUI_FitCameraToBox(&pGen->ScreenBox, bScaleToHeight, bZoom, bAllowMouseWheel, pPlayerCostume, pStance, pCameraView);
}


void costumeCameraUI_CostumeCreatorCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed)
{
	Mat4 camera_matrix;

	if (mouseIsLocked())
	{
		mouseLock(0);
	}

	elapsed = g_ui_State.timestep;

	if( elapsed > 0 )
	{
		int x = 0, y = 0;
		mouseDiffLegacy(&x, &y);
		if (s_bMouseRotate3DFlag)
		{
			s_fYawSpeed = x / (-360 * elapsed);
			s_fPitchSpeed = y / (-360 * elapsed);
		}
		else if (s_bMouseRotateFlag)
		{
			s_fYawSpeed = x / (-360 * elapsed);
		}
	}

	if( s_bPointCameraAtCostume )
	{
		// Build a camera matrix looking from CamPos to TargetPos
		// vAt = Normalize(vCamPos - vTargetPos) // This is backwards because our -Z axis goes out from the camera
		// vRight = Normalize(Cross(vUp, vAt))
		// vUp = Normalize(Cross(vAt, vRight))
		// camera_matrix = {vRight, vUp, vAt, CamPos}
		Vec3 vRight, vUp, vAt;
		if( g_pCostumeView )
		{
			copyVec3(g_pCostumeView->costume.v3SkelPos, s_vInterpCamCenter);
		}

		if( s_fInterpCamSeconds > 0.001f )
		{
			F32 fElapsedMax = MIN(elapsed, s_fInterpCamSeconds);
			F32 t = fElapsedMax / s_fInterpCamSeconds;
			t = 1.0f - powf(1.0f-t, 5); // Quintic EaseOut
			lerpVec3(s_vInterpCamPos, t, camera->camPos, camera->camPos);
			lerpVec3(s_vInterpCamCenter, t, camera->camcenter, camera->camcenter);
			s_fInterpCamSeconds -= fElapsedMax;
		}
		else
		{
			// Snap to 
			copyVec3(s_vInterpCamPos, camera->camPos);
			copyVec3(s_vInterpCamCenter, camera->camcenter);
		}

		setVec3(vUp, 0, 1, 0);
		subVec3(camera->camPos, camera->camcenter, vAt);
		if( lengthVec3Squared(vAt) > FLT_EPSILON )
		{
			normalVec3(vAt);
			crossVec3(vUp, vAt, vRight);
			normalVec3(vRight);
			crossVec3(vAt, vRight, vUp);
			normalVec3(vUp);

			copyVec3(        vRight, camera_matrix[0]);
			copyVec3(           vUp, camera_matrix[1]);
			copyVec3(           vAt, camera_matrix[2]);
			copyVec3(camera->camPos, camera_matrix[3]);
		}
		else
		{
			copyMat3(unitmat, camera_matrix);
		}
	}
	else
	{
		// Actually load it into the matrix, by transforming the camera space movement into world space 
		createMat3YPR(camera_matrix, camera->campyr);
	}

	{
		Vec3 local_offset;
		setVec3(local_offset, 0, 0, camera->camdist);
		addVec3(local_offset, camera->centeroffset, local_offset);
		mulVecMat3(local_offset, camera_matrix, camera_matrix[3]);
		addVec3(camera_matrix[3], camera->camcenter, camera_matrix[3]);
		copyVec3(camera->camcenter, camera->camfocus);
	}

	frustumSetCameraMatrix(&camera_view->new_frustum, camera_matrix);
}

GfxCameraController *costumeCameraUI_GetCamera(void)
{
	return &s_Camera;
}

void costumeCameraUI_CameraOncePerFrame(CostumeViewGraphics *pCameraView, GfxCameraView *pGFXCameraView, bool bCreatorActive)
{
	static int iCostumeIdx;

	Vec3 v3Pos = {0, 0, 0};
	Quat qRot, qRot1, qRot2;
	Quat qPitch, qYaw, qRoll;
	F32 fStickX;
	F32 fStickY;
	F32 rot;
	F32 timeStep = g_ui_State.timestep;
	S32 i;

	if (!pCameraView)
		return;

	gamepadGetRightStick(&fStickX, &fStickY);

	if (!pCameraView->costume.bPositionInCameraSpace) {
		// Need to modify the yaw to naturally face the camera
		GfxCameraController *pCam = costumeView_GetCamera();
		if (s_v3OldCamPos[0] != pCam->camcenter[0] || s_v3OldCamPos[1] != pCam->camcenter[1] || s_v3OldCamPos[2] != pCam->camcenter[2]) {
			// Reset yaw
			Mat4 xCameraMatrix;
			createMat3YPR(xCameraMatrix, pCam->campyr);
			copyVec3(pCam->centeroffset, v3Pos);
			v3Pos[2] += pCam->camdist;
			mulVecMat3(v3Pos, xCameraMatrix, xCameraMatrix[3]);
			addVec3(xCameraMatrix[3], pCam->camcenter, xCameraMatrix[3]);
			yawQuat(atan2f(v3Pos[2] - pCameraView->costume.v3SkelPos[2], v3Pos[0] - pCameraView->costume.v3SkelPos[0]), qRot);
			copyVec3(pCam->camcenter, s_v3OldCamPos);
		} else {
			copyQuat(pCameraView->costume.qSkelRot,qRot);
		}
	} else {
		copyQuat(pCameraView->costume.qSkelRot,qRot);
	}

	rot = 0;
	rot += ROTATION_SPEED * fStickX * s_fRotMult * timeStep;
	rot += ROTATION_SPEED * s_fYawSpeed * s_fRotMult * timeStep;
	yawQuat(-rot, qRot1);
	quatMultiply(qRot, qRot1, qRot2);

	rot = 0;
	rot += ROTATION_SPEED * s_fPitchSpeed * s_fRotMult * timeStep;
	if (s_bMouseRotate3DFlag) rot += ROTATION_SPEED * fStickY * s_fRotMult * timeStep;
	pitchQuat(rot, qRot1);
	quatMultiply(qRot2, qRot1, qRot);

	copyVec3(pCameraView->costume.v3SkelPos, v3Pos);
	//v3Pos[2] += ZOOM_SPEED * s_fZoomSpeed * s_fZoomMult * timeStep;

	rot = 0;
	rot += ROTATION_SPEED * s_fShipPitchSpeed * s_fRotMult * timeStep;
	pitchQuat(rot, qPitch);

	rot = 0;
	rot += ROTATION_SPEED * s_fShipYawSpeed * s_fRotMult * timeStep;
	yawQuat(rot, qYaw);

	rot = 0;
	rot += ROTATION_SPEED * s_fShipRollSpeed * s_fRotMult * timeStep;
	rollQuat(rot, qRoll);

	quatMultiply(qRoll, qRot, qRot1);
	quatMultiply(qYaw, qRot1, qRot);
	quatMultiply(qPitch, qRot, qRot1);

	costumeView_SetPosRot(pCameraView, v3Pos, qRot1);

	if (pCameraView == g_pCostumeView) {
		for (i = 0; i < eaSize(&g_pCostumeView->eaExtraCostumes); i++) {
			CostumeViewCostume *pViewCostume = g_pCostumeView->eaExtraCostumes[i];
			BackgroundPlayerCostume *pBackgroundCostume = eaIndexedGetUsingString(&s_eaBackgroundCostumes, pViewCostume->pcName);

			if (!pBackgroundCostume) {
				continue;
			}

			if (!IS_HANDLE_ACTIVE(pViewCostume->hWLCostume) || pBackgroundCostume->bDirty) {
				PlayerCostume *pCostume = GET_REF(pBackgroundCostume->hCostume);
				if (pCostume) {
					costumeView_RegenViewCostume(pViewCostume,
					                             pCostume,
					                             costumeLoad_GetSlotType(pBackgroundCostume->pcSlotType),
					                             GET_REF(pBackgroundCostume->hMood),
					                             GET_REF(pBackgroundCostume->hClass));
					pBackgroundCostume->bDirty = false;
				}
			}
		}
	}

	if (!s_bHideCostume && bCreatorActive) {
		costumeView_Draw(pCameraView);
	}
}


void costumeCameraUI_DrawGhosts(void) 
{
	GfxCameraController *pCam;
	if (emIsEditorActive()) {
		return;
	}
	if (GSM_IsStateActive(GCL_LOGIN))
	{
		pCam = gclLoginGetCameraController();
	}
	else
	{
		pCam = &s_Camera;
	}

	if (!s_bHideCostume && !s_iSuppressCostume && CostumeUI_IsCreatorActive())
	{
		costumeView_DrawGhosts(g_pCostumeView);
	}

	// Suppress hides for the given number of frames
	if (s_iSuppressCostume) {
		--s_iSuppressCostume;
		s_bRecalcHeight = true; // Reset camera fit as necessary
	}
}


void costumeCameraUI_CreateWorld(GfxCameraView *pCamView)
{
	GfxCameraController *pCam;
	bool bPrevInited;
	Mat4 camera_matrix;

	if (!g_pCostumeView)
	{
		g_pCostumeView = costumeView_CreateGraphics();
		g_pCostumeView->costume.bPositionInCameraSpace = true;
	}
	costumeCameraUI_CamViewReset();
	if (GSM_IsStateActive(GCL_LOGIN))
	{
		pCam = gclLoginGetCameraController();
		g_pCostumeView->bOverrideSky = false;
		g_pCostumeView->pcSkyOverride = NULL;
	}
	else
	{
		pCam = &s_Camera;
		g_pCostumeView->bOverrideSky = true;
		g_pCostumeView->pcSkyOverride = "Sky_Player_Costume_Creator";
	}
	bPrevInited = pCam->inited;
	gfxInitCameraController(pCam, costumeCameraUI_CostumeCreatorCamFunc, NULL);
	pCam->do_shadow_scaling = 1;
	pCam->inited = bPrevInited;
	gfxInitCameraView(pCamView);
	pCamView->adapted_light_range = 2;
	pCamView->adapted_light_range_inited = true;
	costumeView_SetCamera(pCam);

	costumeView_InitGraphics();
	pCam->override_disable_shadows = 0; // CD: turn on shadows in costume UI because Chamberlain hates me and wants me to get lots of bug reports
	pCam->override_disable_shadow_pulse = 1; // CD: turn off shadow pulsing in costume UI
	pCam->useHorizontalFOV = true;
	gGCLState.bHideWorld = s_bCreatorHidesWorld;

	g_pCostumeView->bOverrideTime = true;
	g_pCostumeView->fTime = 10.0;

	// Override the Camera
	gclSetOverrideCameraActive(pCam, pCamView);

	// Make sure the camera uses the proper frustrum
	createMat3YPR(camera_matrix, pCam->campyr);
	camera_matrix[3][0] = pCam->centeroffset[0];
	camera_matrix[3][1] = pCam->centeroffset[1];
	camera_matrix[3][2] = pCam->camdist;
	gfxSetActiveCameraMatrix(camera_matrix,true);
	gfxSetActiveCameraMatrix(camera_matrix,false);

	setVec3(s_v3OldCamPos, 0, 0, 0);
}


// --------------------------------------------------------------------------
// Expression Functions
// --------------------------------------------------------------------------


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetTailorCamMinDist);
float costumeCameraUI_GetTailorCamMinDistFunc(void)
{
	COSTUME_UI_TRACE_FUNC();
	return costumeCameraUI_GetTailorCamMinDist(g_CostumeEditState.pConstCostume);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_GetTailorCamMaxDist);
float costumeCameraUI_GetTailorCamMaxDistFunc(void)
{
	COSTUME_UI_TRACE_FUNC();
	return costumeCameraUI_GetTailorCamMaxDist(g_CostumeEditState.pConstCostume);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_ResetCameraFit);
void costumeCamearaUI_ResetCameraFit(void)
{
	COSTUME_UI_TRACE_FUNC();
	costumeView_ResetCamera();
	s_bRecalcHeight = true;
	s_fFixedHeightMod = 0.0;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetHeightMod);
void costumeCameraUI_SetHeightMod(F32 fValue)
{
	COSTUME_UI_TRACE_FUNC();
	s_fFixedHeightMod = fValue;
}


//fit the camera position relative to character to match the specified gen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_FitCameraToGen);
void costumeCameraUI_FitCameraToGen(SA_PARAM_NN_VALID UIGen *pGen, bool bScaleToHeight, bool bZoom )
{
	COSTUME_UI_TRACE_FUNC();
	costumeCameraUI_FitCameraToGenInternal(pGen, bScaleToHeight, bZoom, bZoom, g_CostumeEditState.pConstCostume, g_CostumeEditState.pStance, g_pCostumeView);
}

//fit the camera position relative to character to match the specified gen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_FitCameraToGenWithMouseWheel);
void costumeCameraUI_FitCameraToGenWithMouseWheel(SA_PARAM_NN_VALID UIGen *pGen, bool bScaleToHeight, bool bZoom, bool bAllowMouseWheel )
{
	COSTUME_UI_TRACE_FUNC();
	costumeCameraUI_FitCameraToGenInternal(pGen, bScaleToHeight, bZoom, bAllowMouseWheel, g_CostumeEditState.pConstCostume, g_CostumeEditState.pStance, g_pCostumeView);
}

extern bool g_MirrorSelectMode;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_FitCostumeToGen);
void costumeCameraUI_FitCostumeToGen(SA_PARAM_NN_VALID UIGen *pGen, bool bScaleToHeight, F32 fOriginX, F32 fOriginY, F32 fZoom)
{
	PlayerCostume *pCostume = g_CostumeEditState.pConstHoverCostume ? g_CostumeEditState.pConstHoverCostume : g_CostumeEditState.pConstCostume;
	PCBoneDef *pSelectedBone = NULL;
	PCBoneDef *pMirrorBone = NULL;
	PCBoneDef *pCenterBone = NULL;
	const char *pcSelectedBone = "Cranium";
	const char *pcCenterBone = "Hips";
	COSTUME_UI_TRACE_FUNC();

	if (!pCostume) {
		return;
	}

	if (pSelectedBone) {
		pMirrorBone = GET_REF(pSelectedBone->hMirrorBone);
	}

	costumeCameraUI_FitCostumeToGenInternal(pGen, bScaleToHeight, fOriginX, fOriginY, fZoom, pCostume, pcSelectedBone, pcCenterBone, pMirrorBone && g_MirrorSelectMode, g_CostumeEditState.pStance, g_pCostumeView);
}

//fit the camera position relative to character to match the specified gen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetCamera);
void costumeCameraUI_SetCamera(SA_PARAM_NN_VALID UIGen *pGen, float fWidth, float fHeight, float fZoom )
{
	Vec3 vPos = {fWidth, fHeight, fZoom};
	COSTUME_UI_TRACE_FUNC();
	if (!g_pCostumeView) return;
	costumeView_SetPosRot(g_pCostumeView, vPos, unitquat);
}


//fit the camera position relative to character to match the specified gen
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SnapCamera);
void costumeCameraUI_SnapCamera(SA_PARAM_NN_VALID UIGen *pGen, int x, int y, int z) // Each = -1/0/1
{
	Vec3 vPos = {0, 0, 0};
	Quat qRot;
	COSTUME_UI_TRACE_FUNC();
	if (!g_pCostumeView) return;

	if (z == 1)
	{
		yawQuat(PI, qRot);
	}
	else if (y == 1)
	{
		pitchQuat(PI/2.0, qRot);
	}
	else if (y == -1)
	{
		pitchQuat(PI/-2.0, qRot);
	}
	else if (x == 1)
	{
		yawQuat(PI/2.0, qRot);
	}
	else if (x == -1)
	{
		yawQuat(PI/-2.0, qRot);
	}
	else
	{
		unitQuat(qRot);
	}

	costumeView_SetPosRot(g_pCostumeView, vPos, qRot);
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_DisableCameraSpacePositioning);
void costumeCameraUI_DisableCameraSpacePositioning(F32 fCostumeX, F32 fCostumeY, F32 fCostumeZ)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView)
	{
		g_pCostumeView->costume.bPositionInCameraSpace = false;
		setVec3(g_pCostumeView->costume.v3SkelPos, fCostumeX, fCostumeY, fCostumeZ);
	}
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_EnableCameraSpacePositioning);
void costumeCameraUI_EnableCameraSpacePositioning(void)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView)
	{
		g_pCostumeView->costume.bPositionInCameraSpace = true;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetCostumeViewPosition);
void costumeCameraUI_SetCostumeViewPosition(F32 fCostumeX, F32 fCostumeY, F32 fCostumeZ)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView)
	{
		setVec3(g_pCostumeView->costume.v3SkelPos, fCostumeX, fCostumeY, fCostumeZ);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetNeverKeepCostumeViewSequencers);
void CostumeCreator_SetNeverKeepCostumeViewSequencers(bool bEnable)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView)
		g_pCostumeView->costume.bNeverKeepSequencers = bEnable;
}


// rotation in radians
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetCostumeViewRotation);
void costumeCameraUI_SetCostumeViewRotation(F32 fRotX, F32 fRotY, F32 fRotZ)
{
	COSTUME_UI_TRACE_FUNC();
	if (g_pCostumeView)
	{
		Vec3 pyr = {fRotX, fRotY, fRotZ};
		PYRToQuat(pyr, g_pCostumeView->costume.qSkelRot);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetCostumeViewCameraPosition);
void costumeCameraUI_SetCostumeViewCameraPosition(F32 fCameraX, F32 fCameraY, F32 fCameraZ)
{
	COSTUME_UI_TRACE_FUNC();
	if( s_bPointCameraAtCostume )
		setVec3(s_vInterpCamPos, fCameraX, fCameraY, fCameraZ);
	else
		setVec3(gclLoginGetCameraController()->camPos, fCameraX, fCameraY, fCameraZ);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetCostumeViewCameraPositionWithTime);
void costumeCameraUI_SetCostumeViewCameraPositionWithTime(F32 fCameraX, F32 fCameraY, F32 fCameraZ, F32 fSeconds)
{
	costumeCameraUI_SetCostumeViewCameraPosition(fCameraX, fCameraY, fCameraZ);
	s_fInterpCamSeconds = fSeconds;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetPointCameraAtCostume);
void costumeCameraUI_SetPointCameraAtCostume(bool bPointAtCostume)
{
	COSTUME_UI_TRACE_FUNC();
	s_bPointCameraAtCostume = bPointAtCostume;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeUI_SetCameraDistanceHack);
void costumeCameraUI_SetCameraDistanceHack(float fMinDistanceModifier, float fMaxDistanceModifier)
{
	// TODO: delete me
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.ViewReset") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_CamViewReset(void);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_ViewReset);
void costumeCameraUI_CamViewReset(void)
{
	COSTUME_UI_TRACE_FUNC();
	s_fYawSpeed = 0;
	s_fPitchSpeed = 0;
	s_fShipYawSpeed = 0;
	s_fShipPitchSpeed = 0;
	s_fShipRollSpeed = 0;
	s_fZoomSpeed = 0;

	costumeView_SetPosRot(g_pCostumeView, vDefaultCharPos, unitquat);
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.MouseRotate") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_SetMouseRotate(bool bRotate);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_MouseRotate);
void costumeCameraUI_SetMouseRotate(bool bRotate)
{
	COSTUME_UI_TRACE_FUNC();
	s_bMouseRotateFlag = bRotate;
	if (!bRotate)
		s_fYawSpeed = 0;
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.MouseRotate3D") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_SetMouseRotate3D(bool bRotate);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_MouseRotate3D);
void costumeCameraUI_SetMouseRotate3D(bool bRotate)
{
	COSTUME_UI_TRACE_FUNC();
	s_bMouseRotate3DFlag = bRotate;
	if (!bRotate)
	{
		s_fYawSpeed = 0;
		s_fPitchSpeed = 0.0f;
	}
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.Yaw") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_SetYawSpeed(bool bSpin, float fSpeed);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CostumeCreator_Yaw");
void costumeCameraUI_SetYawSpeed(bool bSpin, float fSpeed)
{
	COSTUME_UI_TRACE_FUNC();
	if(bSpin)
		s_fYawSpeed = fSpeed;
	else
		s_fYawSpeed = 0.0f;
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.Pitch") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_SetPitchSpeed(int bSpin, float fSpeed);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_Pitch);
void costumeCameraUI_SetPitchSpeed(int bSpin, float fSpeed)
{
	COSTUME_UI_TRACE_FUNC();
	if(bSpin)
		s_fPitchSpeed = fSpeed;
	else
		s_fPitchSpeed = 0.0f;
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.ShipYaw") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCamearUI_SetShipYawSpeed(bool bSpin, float fSpeed);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_ShipYaw);
void costumeCamearUI_SetShipYawSpeed(bool bSpin, float fSpeed)
{
	COSTUME_UI_TRACE_FUNC();
	if(bSpin)
		s_fShipYawSpeed = fSpeed;
	else
		s_fShipYawSpeed = 0.0f;
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.ShipPitch") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_SetShipPitchSpeed(int bSpin, float fSpeed);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_ShipPitch);
void costumeCameraUI_SetShipPitchSpeed(int bSpin, float fSpeed)
{
	COSTUME_UI_TRACE_FUNC();
	if(bSpin)
		s_fShipPitchSpeed = fSpeed;
	else
		s_fShipPitchSpeed = 0.0f;
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.ShipRoll") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_SetShipRollSpeed(int bSpin, float fSpeed);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_ShipRoll);
void costumeCameraUI_SetShipRollSpeed(int bSpin, float fSpeed)
{
	COSTUME_UI_TRACE_FUNC();
	if(bSpin)
		s_fShipRollSpeed = fSpeed;
	else
		s_fShipRollSpeed = 0.0f;
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.Zoom") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_SetZoomSpeed(int bZoom, float fSpeed);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_Zoom);
void costumeCameraUI_SetZoomSpeed(int bZoom, float fSpeed)
{
	COSTUME_UI_TRACE_FUNC();
	if(bZoom)
		s_fZoomSpeed = fSpeed;
	else
		s_fZoomSpeed = 0.0f;
}


// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetZoomMult") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_SetZoomMult(float fMult);

// Set the costume camera zoom speed multiplier
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetZoomMult);
void costumeCameraUI_SetZoomMult(float fMult)
{
	COSTUME_UI_TRACE_FUNC();
	s_fZoomMult = fMult;
}

// Change the zoom the costume camera by a specific amount
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_ZoomStep);
void costumeCameraUI_SetZoomStep(F32 fStepSize)
{
	COSTUME_UI_TRACE_FUNC();
	s_fZoomStep = fStepSize;
}

// Both a command and expr func
AUTO_COMMAND ACMD_NAME("CostumeCreator.SetRotMult") ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Interface) ACMD_HIDE;
void costumeCameraUI_SetRotMult(float fMult);

// Set the costume rotation speed multiplier
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SetRotMult);
void costumeCameraUI_SetRotMult(float fMult)
{
	COSTUME_UI_TRACE_FUNC();
	s_fRotMult = fMult;
}


// Both a command and expr func
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("CostumeCreator_HideCostume") ACMD_HIDE;
void costumeCameraUI_HideCostume(int iHide);

// Toggle rendering of the costume
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_HideCostume);
void costumeCameraUI_HideCostume(int iHide)
{
	COSTUME_UI_TRACE_FUNC();
	s_bHideCostume = iHide;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_SuppressCostume);
void costumeCameraUI_SetSuppressCostume(int iSuppress)
{
	COSTUME_UI_TRACE_FUNC();
	s_iSuppressCostume = iSuppress;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CostumeCreator_DecrementSuppressCostume);
void costumeCameraUI_DecrementSuppressCostume(int iDecrement)
{
	COSTUME_UI_TRACE_FUNC();
	s_iSuppressCostume -= iDecrement;
	if (s_iSuppressCostume < 0) {
		s_iSuppressCostume = 0;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(IsShowingCostumeItems);
int costumeCameraUI_IsShowingCostumeItems(void)
{
	COSTUME_UI_TRACE_FUNC();
	return s_bShowCostumeItems;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ShowCostumeItems);
void costumeCameraUI_SetShowCostumeItems(int show)
{
	COSTUME_UI_TRACE_FUNC();
	if (s_bShowCostumeItems != show)
	{
		s_bShowCostumeItems = show;
		if ((g_CostumeEditState.pConstCostume || g_CostumeEditState.pConstHoverCostume) && g_pCostumeView) {
			CostumeUI_costumeView_RegenCostume(g_pCostumeView, g_CostumeEditState.pConstHoverCostume ? g_CostumeEditState.pConstHoverCostume : g_CostumeEditState.pConstCostume, g_CostumeEditState.pSlotType, GET_REF(g_CostumeEditState.hMood), NULL, g_CostumeEditState.eaShowItems);
		}
	}
}

// Both a command and expr func
AUTO_COMMAND ACMD_HIDE ACMD_ACCESSLEVEL(0) ACMD_NAME("CharacterCreation.HideWorld") ACMD_HIDE;
void costumeCameraUI_HideWorld(bool bHide);

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_HideWorld);
void costumeCameraUI_HideWorld(bool bHide)
{
	COSTUME_UI_TRACE_FUNC();
	s_bCreatorHidesWorld = bHide;
}


AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_GetCamDist);
F32 costumeCameraUI_GetCamDist()
{
	COSTUME_UI_TRACE_FUNC();
	return costumeView_GetCamera() ? costumeView_GetCamera()->camdist : 0.0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetCamDist);
void costumeCameraUI_SetCamDist(F32 fCamDist)
{
	COSTUME_UI_TRACE_FUNC();
	if( costumeView_GetCamera() )
		costumeView_GetCamera()->camdist = fCamDist;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetCamCenterOffset);
void costumeCameraUI_SetCamCenterOffset(F32 fX, F32 fY, F32 fZ)
{
	COSTUME_UI_TRACE_FUNC();
	if( costumeView_GetCamera() )
		setVec3(costumeView_GetCamera()->centeroffset, fX, fY, fZ);
}

void costumeCameraUI_SetFxRegion(WorldRegion *pFxRegion)
{
	if (g_pCostumeView) {
		g_pCostumeView->costume.pFxRegion = pFxRegion;
		g_pCostumeView->costume.bResetFX = true;
		g_pCostumeView->costume.bResetFXManager = true;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetCameraFOV);
void exprCharacterCreation_SetCameraFOV(F32 fFOV)
{
	if( costumeView_GetCamera() )
	{
		costumeView_GetCamera()->projection_fov = fFOV;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_AddBackgroundCostume);
void costumeCameraUI_AddBackgroundCostume(const char *pchCostumeName)
{
	BackgroundPlayerCostume *pRef;
	CostumeViewCostume *pViewCostume;

	if (!g_pCostumeView)
		return;

	pchCostumeName = allocAddString(pchCostumeName);
	pRef = eaIndexedGetUsingString(&s_eaBackgroundCostumes, pchCostumeName);
	if (pRef)
		return;

	if (!eaIndexedGetTable(&s_eaBackgroundCostumes)) {
		eaIndexedEnable(&s_eaBackgroundCostumes, parse_BackgroundPlayerCostume);
	}

	pRef = StructCreate(parse_BackgroundPlayerCostume);
	pRef->pcName = pchCostumeName;
	SET_HANDLE_FROM_STRING(g_hPlayerCostumeDict, pchCostumeName, pRef->hCostume);
	eaPush(&s_eaBackgroundCostumes, pRef);

	pViewCostume = costumeView_CreateViewCostume(g_pCostumeView);
	pViewCostume->pcName = pchCostumeName;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetBackgroundCostumePosition);
void costumeCameraUI_SetBackgroundCostumePosition(const char *pchCostumeName, F32 fX, F32 fY, F32 fZ)
{
	S32 i;

	if (!g_pCostumeView)
		return;

	pchCostumeName = allocAddString(pchCostumeName);
	for (i = 0; i < eaSize(&g_pCostumeView->eaExtraCostumes); i++)
	{
		if (g_pCostumeView->eaExtraCostumes[i]->pcName == pchCostumeName)
		{
			setVec3(g_pCostumeView->eaExtraCostumes[i]->v3SkelPos, fX, fY, fZ);
			break;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_SetBackgroundCostumeYaw);
void costumeCameraUI_SetBackgroundCostumeYaw(const char *pchCostumeName, F32 fYaw)
{
	S32 i;

	if (!g_pCostumeView)
		return;

	pchCostumeName = allocAddString(pchCostumeName);
	for (i = 0; i < eaSize(&g_pCostumeView->eaExtraCostumes); i++)
	{
		if (g_pCostumeView->eaExtraCostumes[i]->pcName == pchCostumeName)
		{
			Vec3 v3Rot = { 0, RAD(fYaw), 0 };
			PYRToQuat(v3Rot, g_pCostumeView->eaExtraCostumes[i]->qSkelRot);
			break;
		}
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(CharacterCreation_RemoveBackgroundCostume);
void costumeCameraUI_RemoveBackgroundCostume(const char *pchCostumeName)
{
	S32 i;

	pchCostumeName = allocAddString(pchCostumeName);
	for (i = 0; i < eaSize(&s_eaBackgroundCostumes); i++)
	{
		if (REF_STRING_FROM_HANDLE(s_eaBackgroundCostumes[i]->hCostume) == pchCostumeName)
		{
			StructDestroy(parse_BackgroundPlayerCostume, eaRemove(&s_eaBackgroundCostumes, i));
			break;
		}
	}

	if (g_pCostumeView)
	{
		for (i = 0; i < eaSize(&g_pCostumeView->eaExtraCostumes); i++)
		{
			if (g_pCostumeView->eaExtraCostumes[i]->pcName == pchCostumeName)
			{
				costumeView_FreeViewCostume(eaRemove(&g_pCostumeView->eaExtraCostumes, i), true);
				break;
			}
		}
	}
}

#include "AutoGen/gclCostumeCameraUI_c_ast.c"
