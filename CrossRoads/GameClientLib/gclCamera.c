/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/
#include "Expression.h"
#include "gclCamera.h"
#include "gfxPrimitive.h"
#include "rand.h"

#include "dynAnimInterface.h"
#include "dynSequencer.h"
#include "dynSkeleton.h"
#include "gclEntity.h"
#include "EntityMovementDefault.h"
#include "Player.h"
#include "PowerModes.h"
#include "RegionRules.h"

#include "fileutil.h"
#include "FolderCache.h"
#include "GraphicsLib.h"
#include "inputMouse.h"
#include "inputGamepad.h"
#include "EditLib.h"

#include "WorldLib.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "wlTime.h"

#include "gclCutscene.h"
#include "gclReticle.h"
#include "GameClientLib.h"
#include "CBox.h"
#include "Character.h"
#include "gclDemo.h"
#include "ClientTargeting.h"
#include "Character_combat.h"
#include "gclBasicOptions.h"
#include "gclControlScheme.h"
#include "gclCommandParse.h"
#include "CombatConfig.h"
#include "PowersMovement.h"
#include "UIGen.h"
#include "gclPlayerControl.h"
#include "wininclude.h"
#include "inputLib.h"
#include "PhysicsSDK.h"

#include "AutoGen/gclCamera_h_ast.h"
#include "AutoGen/WorldCellEntry_h_ast.h"

#include "EntityIterator.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

//global default camera pSettings
CameraSettings g_CameraSettings;
CameraSettings g_DefaultCameraSettings = {0};
static DefaultCameraModeSettings s_DefaultCameraModeSettings = {0};

bool g_bTemporarilyDisableCameraControls;
bool g_bLockPitch = false;
bool g_bLockYaw = false;
bool g_bDoNotResetTargetPitch = false;
bool g_bDoNotResetTargetYaw = false;
F32 g_fHeldYaw = 0.0f;
F32 g_fHeldPitch = 0.0f;
F32 g_fMinPitch = -PI*0.49f;
F32 g_fMaxPitch = PI*0.49f;
extern ControlScheme g_CurrentScheme;

static const F32 s_fAutoAdjustTimeout = 2.f;
static const F32 s_fCameraDistResetTime = 0.75f;
static EntityRef s_erFollowOverride;


#define CAMERA_OBSTRUCTING_OBJECT_ALPHA 0.4f
#define MIN_CAMERA_DISTANCE			(15.f)

#define MAX_IGNORABLE_RAYCAST_HITS 2

static S32 s_bDisableCameraCollision = 0;
AUTO_CMD_INT(s_bDisableCameraCollision, DisableCameraCollision);

static void gclCamera_UpdateCameraLookAtOverride(GfxCameraController* pCamera, CameraSettings* pSettings, const Vec2 vCamMovePY, bool bRotated, F32 fElapsedTime);
static void gclCamera_UpdateMouseInputDamping(CameraSettings *pSettings, GfxCameraController *pCamera, F32 fElapsedTime, Vec2 vCamMovePYInOut);

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(cam_set_min_pitch);
void gclCamera_SetPitchLowerBound(F32 fBound)
{
	g_fMinPitch = fBound;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(cam_set_max_pitch);
void gclCamera_SetPitchUpperBound(F32 fBound)
{
	g_fMaxPitch = fBound;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(cam_lock_pitch);
void gclCamera_SetLockPitch(bool bLock)
{
	g_bLockPitch = bLock;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(cam_lock_yaw);
void gclCamera_SetLockYaw(bool bLock)
{
	g_bLockYaw = bLock;
}

static void gclCamera_SetPYInterpSpeed(CameraSettings *pSettings, F32 fRadsPerSec)
{
	pSettings->pyrInterpSpeed[0] = fRadsPerSec;
	pSettings->pyrInterpSpeed[1] = fRadsPerSec;
	pSettings->pyrInterpSpeed[2] = 0.0f;
}

static void gclResetPYRInterpSpeed( CameraSettings *pSettings)
{
	CameraInterpSpeed* pInterpSpeed = 
		eaIndexedGetUsingInt(&pSettings->eaRotInterpSpeeds, pSettings->eDefaultRotInterpSpeed);

	if (pInterpSpeed)
	{
		gclCamera_SetPYInterpSpeed(pSettings, pInterpSpeed->fSpeed);
	}
	pSettings->fRotInterpBasis = pSettings->fDefaultRotInterpBasis[kRotationalInterpType_Default];
	pSettings->fRotInterpNormMin = pSettings->fDefaultRotInterpNormMin[kRotationalInterpType_Default];
	pSettings->fRotInterpNormMax = pSettings->fDefaultRotInterpNormMax[kRotationalInterpType_Default];
}


static void gclCamera_ResetPYRInterpToController(CameraSettings *pSettings)
{
	CameraInterpSpeed* pInterpSpeed = 
		eaIndexedGetUsingInt(&pSettings->eaRotInterpSpeeds, ECameraInterpSpeed_CONTROLLER);

	if (pInterpSpeed)
	{
		gclCamera_SetPYInterpSpeed(pSettings, pInterpSpeed->fSpeed);
	}

	pSettings->fRotInterpBasis = pSettings->fDefaultRotInterpBasis[kRotationalInterpType_Controller];
	pSettings->fRotInterpNormMin = pSettings->fDefaultRotInterpNormMin[kRotationalInterpType_Controller];
	pSettings->fRotInterpNormMax = pSettings->fDefaultRotInterpNormMax[kRotationalInterpType_Controller];
}

static void gclResetDistInterpSpeed(CameraSettings *pSettings)
{
	Entity* pEnt = entActivePlayerPtr();
	RegionRules* pRules = pEnt ? getRegionRulesFromEnt(pEnt) : NULL;
	if (pRules && pRules->fCamDistInterpSpeed > 0.0f)
	{
		pSettings->fDistanceInterpSpeed = pRules->fCamDistInterpSpeed;
	}
	else
	{
		pSettings->fDistanceInterpSpeed = g_DefaultCameraSettings.fDistanceInterpSpeed;
	}
}

void gclCamera_UpdateNearOffset(SA_PARAM_NN_VALID Entity* pEnt)
{
	RegionRules* pRules = pEnt ? getRegionRulesFromEnt(pEnt) : NULL;
	bool bEnable;

	if (!pRules)
	{
		if (g_CurrentScheme.bDisableDefaultOffset)
		{
			return;
		}
		else
		{
			bEnable = true;
		}
	}
	else
	{
		bEnable = g_CurrentScheme.bForceCameraNearOffset ? true : pRules->bCameraNearOffset;
	}
	gclCamera_UseNearOffset(bEnable);
}

static void gclCamera_SetFadeDistanceScale(F32 fFadeScale)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->fFadeDistanceScale = fFadeScale;
	}
}

static void gclCamera_SetDefaultCamAdjustDistances(F32 fCloseAdjustDistance, F32 fDefaultAdjustDistance)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		if (fCloseAdjustDistance > FLT_EPSILON)
			pSettings->fCloseAdjustDistance = fCloseAdjustDistance;
		if (fDefaultAdjustDistance > FLT_EPSILON)
			pSettings->fDefaultAdjustDistance = fDefaultAdjustDistance;
	}
}

void gclCamera_UpdateRegionSettings(SA_PARAM_NN_VALID Entity* pEnt)
{
	RegionRules* pRules = getRegionRulesFromEnt(pEnt);

	gclCamera_UpdateModeForRegion(pEnt);

	if (pRules)
	{
		gclCamera_UseFlightZoom(pRules->bCameraFlightZoom);
		gclCamera_UpdateNearOffset(pEnt);

		if (pRules->pCamDistPresets)
		{
			F32 fFarthestMin = pRules->pCamDistPresets->fMaxZoomMin;
			F32 fFarthestMax = pRules->pCamDistPresets->fMaxZoomMax;
			F32 fClose = pRules->pCamDistPresets->fClose;
			F32 fMedium = pRules->pCamDistPresets->fMedium;
			F32 fFar = pRules->pCamDistPresets->fFar;
			gclCamera_SetMaxDistanceRanges(fFarthestMin, fFarthestMax);
			gclCamera_AdjustDistancePresets(fClose,fMedium,fFar);
			gclCamera_SetClosestDistance(pRules->pCamDistPresets->fClosest);
			gclCamera_SetFadeDistanceScale(pRules->pCamDistPresets->fFadeDistanceScale);
			gclCamera_SetDefaultCamAdjustDistances(pRules->pCamDistPresets->fCloseAdjustDistance, pRules->pCamDistPresets->fDefaultAdjustDistance);
		}
	}
}

//sets the passed pSettings to arbitrary default values
static void gclInitDefaultCameraSettings(CameraSettings *pSettings, bool bIsReload)
{
	Entity* pEnt = entActivePlayerPtr();
	StructCopyAll(parse_CameraSettings, &g_DefaultCameraSettings, pSettings);

	pSettings->iCurrentPreset = 0;

	pSettings->fDistance = pSettings->pfDistancePresets[pSettings->iCurrentPreset];
	pSettings->fAutoLevelPitch = pSettings->v3AutoLevelPitchPresets[pSettings->iCurrentPreset];

	gclResetPYRInterpSpeed( pSettings );

	gclResetDistInterpSpeed( pSettings );

	pSettings->bPlayerIsBlocked = false;
	pSettings->fLastPlayerYaw = 0.0;
	pSettings->fCameraCenterOffset = 0.0f;
	pSettings->bSmoothCamera = false;

	if (nearSameF32(pSettings->fZoomMultWeightLargeEntity, 0.0f))
	{
		pSettings->fZoomMultWeightLargeEntity = pSettings->fZoomMultWeight;
	}

	if (pEnt && bIsReload)
	{
		gclCamera_UpdateRegionSettings(pEnt);
		schemes_UpdateForCurrentControlScheme(false);
	}

	gclBasicOptions_UpdateCameraOptions();
}

static void gclCamera_ValidateDefaultSettings(void)
{
	CameraSettings* pSettings = &g_DefaultCameraSettings;
	
	if (pSettings->fMouseSensitivityMin > pSettings->fMouseSensitivityMax)
	{
		Errorf("Camera Settings Error: The min mouse sensitivity is greater than the max");
	}
	if (pSettings->fMouseSensitivityDefault < pSettings->fMouseSensitivityMin ||
		pSettings->fMouseSensitivityDefault > pSettings->fMouseSensitivityMax)
	{
		Errorf("Camera Settings Error: The default mouse sensitivity is out of bounds");
	}

	// validate CameraLookatOverrideDef
	if (pSettings->pPowerActivationLookatOverride)
	{
		CameraLookatOverrideDef *pLookatDef = pSettings->pPowerActivationLookatOverride;
		if (pLookatDef->fInterpStartSpeed <= 0.f)
		{
			ErrorFilenamef(	"CameraSettings.def", 
							"Camera Settings PowerActivationLookatOverride: InterpStartSpeed must be greater than zero.");
		}
	}
}

static void gclCamera_LoadSettings(const char *pchPath, S32 iWhen)
{
	bool bIsReload = false;
	loadstart_printf("Loading camera settings...");
	
	StructReset(parse_CameraSettings, &g_DefaultCameraSettings);
	StructReset(parse_DefaultCameraModeSettings, &s_DefaultCameraModeSettings);

	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
		bIsReload = true;
	}

	ParserLoadFiles(NULL, "defs/config/CameraSettings.def", "CameraSettings.bin", 
					PARSER_OPTIONALFLAG, parse_CameraSettings, &g_DefaultCameraSettings);
	ParserLoadFiles(NULL, "defs/config/CameraModeSettings.def", "CameraModeSettings.bin", 
					PARSER_OPTIONALFLAG, parse_DefaultCameraModeSettings, &s_DefaultCameraModeSettings);

	gclInitDefaultCameraSettings(&g_CameraSettings, bIsReload);

	if (isDevelopmentMode())
	{
		gclCamera_ValidateDefaultSettings();
	}
	loadend_printf(" Done.");
}

AUTO_STARTUP(CameraSettings);
void gclCamera_Load(void)
{
	gclCamera_LoadSettings(NULL, 0);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/CameraSettings.def", gclCamera_LoadSettings);
	FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "defs/config/CameraModeSettings.def", gclCamera_LoadSettings);
}

//grab the camera pSettings from the camera controller -> stored in user_data
//unless user_data is NULL, then just return the global settings
CameraSettings* gclCamera_GetSettings(const GfxCameraController *pCamera)
{
	return (pCamera && pCamera->user_data) ? (CameraSettings*)pCamera->user_data : &g_CameraSettings;
}

bool gclCamera_IsValidModeForRegion(CameraMode eMode, WorldRegionType eRegionType)
{
	RegionRules* pRules = getRegionRulesFromRegionType(eRegionType);
	S32 i, j;
	if (!eaSize(&g_CameraTypeRules.eaDefs))
	{
		return true;
	}
	for (i = eaSize(&g_CameraTypeRules.eaDefs)-1; i >= 0; i--)
	{
		CameraTypeRulesDef* pDef = g_CameraTypeRules.eaDefs[i];
		for (j = eaSize(&pDef->eaModeRegions)-1; j >= 0; j--)
		{
			CameraModeRegion* pModeRegion = pDef->eaModeRegions[j];
			if (	eMode == pModeRegion->eMode 
				&& (!pRules || (pModeRegion->eRegions & pRules->eSchemeRegionType)))
			{
				return true;
			}
		}
	}
	return false;
}

CameraType gclCamera_GetTypeFromMode(CameraMode eMode)
{
	S32 i, j;
	for (i = eaSize(&g_CameraTypeRules.eaDefs)-1; i >= 0; i--)
	{
		CameraTypeRulesDef* pDef = g_CameraTypeRules.eaDefs[i];
		for (j = eaSize(&pDef->eaModeRegions)-1; j >= 0; j--)
		{
			CameraModeRegion* pModeRegion = pDef->eaModeRegions[j];
			if (eMode == pModeRegion->eMode)
			{
				return pDef->eType;
			}
		}
	}
	return -1;
}

void gclCamera_UpdateModeForRegion(Entity* pEnt)
{
	if (pEnt)
	{
		RegionRules* pRules = getRegionRulesFromEnt(pEnt);
		S32 i, j;
		for (i = eaSize(&g_CameraTypeRules.eaDefs)-1; i >= 0; i--)
		{
			CameraTypeRulesDef* pDef = g_CameraTypeRules.eaDefs[i];
			if (!pDef->bUserSelectable)
			{
				continue;
			}
			if (pDef->eType == g_CurrentScheme.eCameraType)
			{
				for (j = eaSize(&pDef->eaModeRegions)-1; j >= 0; j--)
				{
					CameraModeRegion* pModeRegion = pDef->eaModeRegions[j];
					if (!pRules || (pModeRegion->eRegions & pRules->eSchemeRegionType))
					{
						gclCamera_SetMode(pModeRegion->eMode, false);
						return;
					}
				}
			}
		}
	}
	gclCamera_SetMode(kCameraMode_Default, false);
}

void gclCamera_SetMaxDistanceRanges(F32 fMin, F32 fMax)
{
	CameraSettings *pSettings = gclCamera_GetSettings(NULL);
	pSettings->fMaxDistanceMinValue = fMin;
	pSettings->fMaxDistanceMaxValue = fMax;
}

void gclCamera_OnMouseLook(U32 bIsMouseLooking)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();

	if ( pCamera )
	{
		CameraSettings *pSettings = gclCamera_GetSettings(pCamera);

		pSettings->bIsMouseLooking = !!bIsMouseLooking;
	}
}

void gclCamera_SetCameraInputTurnLeft(int bIsTurning)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();

	if ( pCamera )
	{
		CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
		pCamera->rotate_left = !!bIsTurning;
	}
}

void gclCamera_SetCameraInputTurnRight(int bIsTurning)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();

	if ( pCamera )
	{
		CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
		pCamera->rotate_right = !!bIsTurning;
	}
}

void gclCamera_SetCameraInputLookUp(int bIsTurning)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();

	if ( pCamera )
	{
		pCamera->rotate_up = !!bIsTurning;
	}
}

void gclCamera_SetCameraInputLookDown(int bIsTurning)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();

	if ( pCamera )
	{
		pCamera->rotate_down = !!bIsTurning;
	}
}

bool gclCamera_IsCameraTurningViaInput()
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();

	if ( pCamera )
	{
		return pCamera->rotate_left || pCamera->rotate_right;
	}

	return false;
}

static void* gclCamera_CreateSettings(ParseTable* pTable, void* pDefaultSettings)
{
	void* pData = StructCreateVoid(pTable);
	if (pDefaultSettings)
	{
		StructCopyAllVoid(pTable, pDefaultSettings, pData);
	}
	return pData;
}

void* gclCamera_GetDefaultSettingsForMode(CameraMode eMode)
{
	DefaultCameraModeSettings* pDefault = &s_DefaultCameraModeSettings;
	switch (eMode)
	{
		xcase kCameraMode_AutoTarget:
		{
			return gclCamera_CreateSettings(parse_AutoTargetSettings, pDefault->pAutoTargetSettings);
		}
		xcase kCameraMode_AimCamera:
		{
			return gclCamera_CreateSettings(parse_AimCameraSettings, pDefault->pAimSettings);
		}
		xcase kCameraMode_ShooterCamera:
		{
			return gclCamera_CreateSettings(parse_ShooterCameraSettings, pDefault->pShooterSettings);
		}
		xcase kCameraMode_ChaseCamera:
		{
			return gclCamera_CreateSettings(parse_ChaseCameraSettings, pDefault->pChaseSettings);
		}
		xcase kCameraMode_TweenToTarget:
		{
			return gclCamera_CreateSettings(parse_TweenCameraSettings, pDefault->pTweenSettings);
		}
		xcase kCameraMode_GiganticCamera:
		{
			return gclCamera_CreateSettings(parse_GiganticCameraSettings, pDefault->pGiganticSettings);
		}
	}
	return NULL;
}

static void gclCamera_ResetSnapToSettings(SnapToSettings* pSnapSettings)
{
	eaClearStruct(&pSnapSettings->eaHistory, parse_CameraSmoothNode);
	pSnapSettings->fAccum = 0.0f;
	pSnapSettings->bSnappedToTarget = false;
}

static void gclSpaceCameraLock_Reset(CameraSettings *pSettings)
{
	if (devassert(pSettings->eMode == kCameraMode_AutoTarget))
	{
		AutoTargetSettings* pAutoSettings = (AutoTargetSettings*)pSettings->pModeSettings;
		pAutoSettings->fDelay = 0.0f;
		gclCamera_ResetSnapToSettings(&pAutoSettings->SnapTo);
		pAutoSettings->eFlags &= ~(kAutoCamLockFlags_HadTargetLastFrame|kAutoCamLockFlags_TweenToHeading|kAutoCamLockFlags_IsInCombat);
	}
}

static void gclSpaceCameraLock_Init(GfxCameraController* pCamera, CameraSettings *pSettings)
{
	AutoTargetSettings* pAutoSettings = (AutoTargetSettings*)pSettings->pModeSettings;
	pAutoSettings->eFlags &= ~(kAutoCamLockFlags_IsTrackingRotate|kAutoCamLockFlags_HasMouseRotated);
	gclSpaceCameraLock_Reset(pSettings);
}

static void gclAimCamera_Init(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	AimCameraSettings* pAimSettings = (AimCameraSettings*)pSettings->pModeSettings;
	pAimSettings->uiNextAimTime = 0;
	pAimSettings->fSavedDistance = pSettings->fDistance;
	pSettings->fDistance = pAimSettings->fDistance;
	gclCamera_ResetSnapToSettings(&pAimSettings->SnapData);
	pSettings->fDistanceInterpSpeed = pAimSettings->fDistInterpSpeed;
}

static void gclAimCamera_DeInit(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	AimCameraSettings* pAimSettings = (AimCameraSettings*)pSettings->pModeSettings;
	pAimSettings->fDistance = pSettings->fDistance;
	pSettings->fDistance = pAimSettings->fSavedDistance;
	gclResetDistInterpSpeed(pSettings);
}

static void gclChaseCamera_Init(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	ChaseCameraSettings* pChaseSettings = (ChaseCameraSettings*)pSettings->pModeSettings;
	pChaseSettings->uiNextChaseTime = 0;
	gclCamera_ResetSnapToSettings(&pChaseSettings->SnapData);
}

static void gclShooterCamera_Init(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	ShooterCameraSettings* pShooterSettings = (ShooterCameraSettings*)pSettings->pModeSettings;
	pShooterSettings->bAimModeInit = false;
	pShooterSettings->fHeightMultOrig = pSettings->fPlayerHeightMultiplier;
	pShooterSettings->bLockOnSnap = false;
	pSettings->bScaleMouseSpeedByFOV = true;
	gclPlayerControl_SetAlwaysUseMouseLookForced(true);
	
	if (pShooterSettings->fHeightMult > FLT_EPSILON)
	{
		pSettings->fPlayerHeightMultiplier = pShooterSettings->fHeightMult;
	}
}

static void gclShooterCamera_DeInit(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	ShooterCameraSettings* pShooterSettings = (ShooterCameraSettings*)pSettings->pModeSettings;
	pShooterSettings->bAimModeInit = false;
	pSettings->fPlayerHeightMultiplier = pShooterSettings->fHeightMultOrig;
	pSettings->bScaleMouseSpeedByFOV = false;
	
	g_bLockYaw = false;
	g_bLockPitch = false;


	gclPlayerControl_SetAlwaysUseMouseLookForced(false);
	gclPlayerControl_UpdateMouseInput(false, false);
	gclCamera_OnMouseLook(false);
}

static void gclTweenToTargetCamera_DeInit(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	gclResetPYRInterpSpeed(pSettings);
	gclResetDistInterpSpeed(pSettings);
}

static void gclGiganticCamera_Init(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	GiganticCameraSettings* pGiganticSettings = (GiganticCameraSettings*)pSettings->pModeSettings;
	Entity* pEnt = gclCamera_GetEntity();
	F32 fPlayerHeight = entGetHeight(pEnt);
	F32 fMinDist = fPlayerHeight * pGiganticSettings->fMinDistMult;
	F32 fMaxDist = fPlayerHeight * pGiganticSettings->fMaxDistMult;

	if(!pGiganticSettings->bPitchInit)
	{
		pCamera->targetpyr[0] = RAD(pGiganticSettings->fInitialPitch);
		pGiganticSettings->bPitchInit = true;
	}

	pGiganticSettings->bHeightInit = false;
	pGiganticSettings->fSavedDistance = pSettings->fDistance;
	pSettings->fDistance = fMinDist + (fMaxDist - fMinDist) * 0.5f;
}

static void gclGiganticCamera_DeInit(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	GiganticCameraSettings* pGiganticSettings = (GiganticCameraSettings*)pSettings->pModeSettings;
	pSettings->fDistance = pGiganticSettings->fSavedDistance;
}

void DEFAULT_LATELINK_gclCamera_GameSpecificInit(void)
{
}

static void gclCamera_InitializeMode(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	switch (pSettings->eMode)
	{
		xcase kCameraMode_AutoTarget:
		{
			gclSpaceCameraLock_Init(pCamera, pSettings);
		}
		xcase kCameraMode_AimCamera:
		{
			gclAimCamera_Init(pCamera, pSettings);
		}
		xcase kCameraMode_ChaseCamera:
		{
			gclChaseCamera_Init(pCamera, pSettings);
		}
		xcase kCameraMode_ShooterCamera:
		{
			gclShooterCamera_Init(pCamera, pSettings);
		}
		xcase kCameraMode_TweenToTarget:
		{
			//gclTweenToTargetCamera_Init(pCamera, pSettings);
		}
		xcase kCameraMode_GiganticCamera:
		{
			gclGiganticCamera_Init(pCamera, pSettings);
		}
	}

	// Do game-specific initialization
	gclCamera_GameSpecificInit();
}

void DEFAULT_LATELINK_gclCamera_GameSpecificDeInit(void)
{
}

static void gclCamera_DeInitializeMode(GfxCameraController* pCamera, CameraSettings* pSettings)
{
	switch (pSettings->eMode)
	{
		xcase kCameraMode_AutoTarget:
		{
			//gclSpaceCameraLock_DeInit(pCamera, pSettings);
		}
		xcase kCameraMode_AimCamera:
		{
			gclAimCamera_DeInit(pCamera, pSettings);
		}
		xcase kCameraMode_ChaseCamera:
		{
			//gclChaseCamera_DeInit(pCamera, pSettings);
		}
		xcase kCameraMode_ShooterCamera:
		{
			gclShooterCamera_DeInit(pCamera, pSettings);
		}
		xcase kCameraMode_TweenToTarget:
		{
			gclTweenToTargetCamera_DeInit(pCamera, pSettings);
		}
		xcase kCameraMode_TurnToFace:
		{
			//gclTurnToFaceCamera_DeInit(pCamera, pSettings);
		}
		xcase kCameraMode_GiganticCamera:
		{
			gclGiganticCamera_DeInit(pCamera, pSettings);
		}
	}

	// Do game-specific deinitialization
	gclCamera_GameSpecificDeInit();
}

void* gclCamera_FindSettingsForMode(SA_PARAM_OP_VALID GfxCameraController* pCamera, CameraMode eMode, bool bCreate)
{
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
	CameraModeSettings* pData = eaIndexedGetUsingInt(&pSettings->eaModeSettings, eMode);
	if (pData)
	{
		return pData->pModeSettings;
	}
	else if (bCreate)
	{
		void* pDefault = gclCamera_GetDefaultSettingsForMode(eMode);
		if (pDefault)
		{
			pData = StructCreate(parse_CameraModeSettings);
			pData->pModeSettings = pDefault;
			pData->eMode = eMode;
			eaIndexedEnable(&pSettings->eaModeSettings, parse_CameraModeSettings);
			devassert(eaPushUnique(&pSettings->eaModeSettings, pData));
		}
		return pDefault;
	}
	return NULL;
}

bool gclCamera_SetLastMode(bool bCheckValidForRegion, bool bUseDefaultOnFail)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		
		if (!gclCamera_SetMode(pSettings->eLastMode, bCheckValidForRegion))
		{
			if (bUseDefaultOnFail)
			{
				return gclCamera_SetMode(kCameraMode_Default, false);
			}
		}
	}
	return false;
}

bool gclCamera_SetMode(CameraMode eMode, bool bCheckValidForRegion)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		bool bValid = CAMERA_MODE_VALID(eMode) && pSettings->eMode != eMode;

		if (bValid && bCheckValidForRegion)
		{
			Entity* pEnt = gclCamera_GetEntity();
			if (!pEnt || !gclCamera_IsValidModeForRegion(eMode,entGetWorldRegionTypeOfEnt(pEnt)))
			{
				bValid = false;
			}
		}
		if (bValid)
		{
			gclCamera_DeInitializeMode(pCamera, pSettings);
			pSettings->eLastMode = pSettings->eMode;
			pSettings->eMode = eMode;
			pSettings->pModeSettings = gclCamera_FindSettingsForMode(pCamera, eMode, true);
			gclCamera_InitializeMode(pCamera, pSettings);
			return true;
		}
	}
	return false;
}

F32 gclCamera_GetDefaultModeDistance(GfxCameraController* pCamera)
{
	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

		switch (pSettings->eMode)
		{
			xcase kCameraMode_AimCamera:
			{
				AimCameraSettings* pAimSettings = (AimCameraSettings*)(pSettings->pModeSettings);
				if (pAimSettings)
				{
					return pAimSettings->fSavedDistance;
				}
			}
			xcase kCameraMode_ShooterCamera:
			{
				ShooterCameraSettings* pShooterSettings = (ShooterCameraSettings*)(pSettings->pModeSettings);
				if (pSettings->bIsInAimMode && pShooterSettings->bAimModeInit)
				{
					return pShooterSettings->fSavedDistance;
				}
			}

			xcase kCameraMode_GiganticCamera:
			{
				GiganticCameraSettings* pGiganticSettings = (GiganticCameraSettings*)(pSettings->pModeSettings);
				if (pGiganticSettings)
				{
					return pGiganticSettings->fSavedDistance;
				}
			}
		}
		return pSettings->fDistance;
	}
	return 0;
}

// Sets the camera distance from the player
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(camdist) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetDistance(F32 fDist)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->fDistance = fDist;
	}
}
// Sets the camera offset from the player
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(camoffset) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetOffset(F32 fOffset)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->fOffset = fOffset;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cam_near) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetNear(F32 fNear)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->pfDistancePresets[0] = fNear;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cam_mid) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetMid(F32 fMid)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->pfDistancePresets[1] = fMid;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cam_far) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetFar(F32 fFar)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->pfDistancePresets[2] = fFar;
	}
}

// Sets the camera distance borders from the player

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cam_pitch_speed) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetPitchSpeed(F32 fSpeed)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->pyrJoypadSpeed[0] = fSpeed;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cam_yaw_speed) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetYawSpeed(F32 fSpeed)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->pyrJoypadSpeed[1] = fSpeed;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cam_pitch_interp) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetPitchInterpSpeed(F32 fInterp)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->pyrInterpSpeed[0] = fInterp;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cam_yaw_interp) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetYawInterpSpeed(F32 fInterp)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->pyrInterpSpeed[1] = fInterp;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cam_distance_interp_speed) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetDistanceInterpSpeed(F32 fSpeed)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->fDistanceInterpSpeed = fSpeed;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(cam_autolevel_interp_speed) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_SetAutoLevelInterpSpeed(F32 fSpeed)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->fAutoLevelInterpSpeed = fSpeed;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camUseHarshTargetLock);
void gclCamera_UseHarshTargetLock(bool bEnabled)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		if (bEnabled)
		{
			gclCamera_SetMode(kCameraMode_HarshTargetLock,true);
		}
		else if (pSettings->eMode == kCameraMode_HarshTargetLock)
		{
			gclCamera_SetLastMode(true,true);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camUseAutoTargetLock);
void gclCamera_UseAutoTargetLock(bool bEnabled)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

		if (bEnabled)
		{
			gclCamera_SetMode(kCameraMode_AutoTarget,true);
		}
		else if (pSettings->eMode == kCameraMode_AutoTarget)
		{
			gclCamera_SetLastMode(true,true);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_HIDE ACMD_NAME(camToggleAutoTargetLock);
void gclCamera_ToggleAutoTargetLock( void )
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

		gclCamera_UseAutoTargetLock(pSettings->eMode != kCameraMode_AutoTarget);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_HIDE ACMD_NAME(camToggleSTOTargetLock) ACMD_PRODUCTS(StarTrek);
void gclCamera_ToggleFollowTargetCameraType( void )
{
	Entity* pEnt = gclCamera_GetEntity();

	if (g_CurrentScheme.eCameraType != kCameraType_Follow)
	{
		g_CurrentScheme.eLastCameraType = g_CurrentScheme.eCameraType;
	}

	g_CurrentScheme.eCameraType = (g_CurrentScheme.eCameraType == kCameraType_Follow) ? g_CurrentScheme.eLastCameraType : kCameraType_Follow;
	gclCamera_UpdateModeForRegion(pEnt);
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camUseChaseCam) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_UseChaseCamera(bool bEnable)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

		if (bEnable)
		{
			gclCamera_SetMode(kCameraMode_ChaseCamera,true);
		}
		else if (pSettings->eMode == kCameraMode_ChaseCamera)
		{
			gclCamera_SetLastMode(true,true);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camToggleChaseCam) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_ToggleChaseCamera( void )
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

		gclCamera_UseChaseCamera(pSettings->eMode != kCameraMode_ChaseCamera);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camUseAimCam) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_UseAimCamera(bool bEnable)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

		if (bEnable)
		{
			gclCamera_SetMode(kCameraMode_AimCamera,true);
		}
		else if (pSettings->eMode == kCameraMode_AimCamera)
		{
			gclCamera_SetLastMode(true,true);
		}
	}
}

bool gclCamera_IsInMode(CameraMode eMode)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		return gclCamera_GetSettings(pCamera)->eMode == eMode;
	}
	return false;
}

bool gclCamera_IsInInspectMode()
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		return gclCamera_GetSettings(pCamera)->bIsInInspectMode;
	}
	return false;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camToggleAimCam) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_ToggleAimCamera( void )
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

		gclCamera_UseAimCamera(pSettings->eMode != kCameraMode_AimCamera);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camTurnToFace) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_UseTurnToFace(bool bEnable)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

		if (bEnable)
		{
			gclCamera_SetMode(kCameraMode_TurnToFace,true);
			gclPlayerControl_DisableFollowThisFrame();
		}
		else if (pSettings->eMode == kCameraMode_TurnToFace)
		{
			gclCamera_SetLastMode(true,true);
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camUseLeashCam) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_UseLeashCamera(bool enabled)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		// disabling the leash camera for now because it is broken and no one seems to be using it
		// todo: fix the leash camera or just remove it!
		//gclCamera_SetMode(kCameraMode_LeashCamera,true);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(cam_smooth);
void gclCamera_SetSmooth(bool bSmooth)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->bSmoothCamera = bSmooth;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_HIDE;
void gclCamera_UseLockToTarget(bool bEnable)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		pCamera->lockedtotarget = !!bEnable;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0)  ACMD_CATEGORY(Standard, Targeting) ACMD_NAME(camButton_Target_Lock_Toggle);
void gclCamera_ToggleTargetLockToTarget()
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		pCamera->lockedtotarget = !pCamera->lockedtotarget;
	}
}



AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camButton_LockControllerControl) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_LockControllerControl(bool bDown)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		pCamera->lockControllerCamControl = bDown;
	}
} 

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camButton_LockAutoAdjust) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_LockAutoAdjust(bool bDown)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		pCamera->lockAutoAdjust = bDown;
		if (bDown)
		{
			copyVec3(pCamera->campyr, pCamera->targetpyr);
			devassertmsg(FINITEVEC3(pCamera->targetpyr), "Undefined pCamera PYR!");
		}
	}
}

// Sets height above ground
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(cam_height);
void gclCamera_AdjustCameraHeight(F32 fHeight)
{	
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->fHeight = fHeight;
	}
}

void gclCamera_UseNearOffset(bool bEnable)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->bUseNearOffset = bEnable;
	}
}

void gclCamera_UseFlightZoom(bool bEnable)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->bFlightZoom = bEnable;
	}
}

void gclCamera_SetClosestDistance(F32 fDist)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		gclCamera_GetSettings(pCamera)->fClosestDistance = fDist;
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(AdjustCamMaxDistance);
void gclCamera_AdjustDistancePresetsFromMax(F32 fMaxDistance)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		F32 fMin = pSettings->fMaxDistanceMinValue;
		F32 fMax = pSettings->fMaxDistanceMaxValue;
		
		fMaxDistance = CLAMP(fMaxDistance, fMin, fMax);
		
		pSettings->pfDistancePresets[0] = fMaxDistance * (1.f / 3.f);
		pSettings->pfDistancePresets[1] = fMaxDistance * (2.f / 3.f);
		pSettings->pfDistancePresets[2] = fMaxDistance;
		pSettings->fDistance = MINF(pSettings->fDistance, pSettings->pfDistancePresets[2]);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(AdjustCamMouseSensitivity);
void gclCamera_SetMouseLookSensitivity(F32 fSensitivity)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	CameraSettings* pSettings = pCamera ? gclCamera_GetSettings(pCamera) : NULL;
	F32 fAdjustedSensitivity;
	
	if (!pCamera)
	{
		return;
	}
	if (nearSameF32(fSensitivity,0.0f))
	{
		fSensitivity = pSettings->fMouseSensitivityDefault;
	}
	else
	{
		fSensitivity = CLAMP(fSensitivity, pSettings->fMouseSensitivityMin, pSettings->fMouseSensitivityMax);
	}

	// Adjust the current sensitivity based on the field of view of the camera
	if (pSettings->bScaleMouseSpeedByFOV)
	{
		fAdjustedSensitivity = fSensitivity * (pCamera->target_projection_fov / gfxGetDefaultFOV());
	}
	else
	{
		fAdjustedSensitivity = fSensitivity;
	}
	pSettings->pyrMouseSpeed[0] = fAdjustedSensitivity;
	pSettings->pyrMouseSpeed[1] = fAdjustedSensitivity;
	pSettings->pyrDefaultMouseSpeed[0] = fSensitivity;
	pSettings->pyrDefaultMouseSpeed[1] = fSensitivity;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(AdjustCamControllerSensitivity);
void gclCamera_SetControllerLookSensitivity(F32 fSensitivity)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

	if (nearSameF32(fSensitivity,0.0f))
	{
		fSensitivity = pSettings->fControllerSensitivityDefault;
	}
	else
	{
		F32 fMin = pSettings->fControllerSensitivityMin;
		F32 fMax = pSettings->fControllerSensitivityMax;
		fSensitivity = CLAMP(fSensitivity, fMin, fMax);
	}

	pSettings->pyrJoypadSpeed[0] = fSensitivity;
	pSettings->pyrJoypadSpeed[1] = fSensitivity;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(AdjustCamDistancePresets);
void gclCamera_AdjustDistancePresets(F32 fClose, F32 fMedium, F32 fFar)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		fFar = CLAMP(fFar, MIN_CAMERA_DISTANCE, pSettings->fMaxDistanceMaxValue);
		fMedium = CLAMP(fMedium, MIN_CAMERA_DISTANCE, fFar);
		fClose = CLAMP(fClose, MIN_CAMERA_DISTANCE, fMedium);
		
		pSettings->pfDistancePresets[0] = fClose;
		pSettings->pfDistancePresets[1] = fMedium;
		pSettings->pfDistancePresets[2] = fFar;
		pSettings->fDistance = MINF(pSettings->fDistance, pSettings->pfDistancePresets[2]);
	}
}

static F32 gclCamera_GetMaxDistance(CameraSettings* pSettings)
{
	switch (pSettings->eMode)
	{
		xcase kCameraMode_GiganticCamera:
		{
			GiganticCameraSettings* pGiganticSettings = (GiganticCameraSettings*)(pSettings->pModeSettings);
			Entity* pEnt = gclCamera_GetEntity();
			F32 fPlayerHeight = entGetHeight(pEnt);
			return fPlayerHeight * pGiganticSettings->fMaxDistMult;
		}
	}
	return pSettings->pfDistancePresets[2];
}

static F32 gclCamera_GetDistanceScale(CameraSettings* pSettings)
{
	switch (pSettings->eMode)
	{
		xcase kCameraMode_GiganticCamera:
		{
			GiganticCameraSettings* pGiganticSettings = (GiganticCameraSettings*)(pSettings->pModeSettings);
			Entity* pEnt = gclCamera_GetEntity();
			F32 fBasePlayerHeight = MAXF(pGiganticSettings->fBasePlayerHeight, 0.01f);
			F32 fPlayerHeight = entGetHeight(pEnt);
			F32 fPlayerHeightRatio = fPlayerHeight / pGiganticSettings->fBasePlayerHeight;
			return fPlayerHeightRatio;
		}
	}
	return 1.0f;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(AdjustCamDistance);
void gclCamera_AdjustDistance(S32 iDelta)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		F32 fMaxDistance = gclCamera_GetMaxDistance(pSettings);
		F32 fScale = gclCamera_GetDistanceScale(pSettings);

		if (iDelta > 0)
		{
			if (pCamera->block_time > 0.0f && pSettings->fDistance > pCamera->targetdist+2.0f)
			{
				// Just throw away any attempts to zoom out while the camera is pegged against a wall
				return;
			}
		}
		else if (iDelta < 0)
		{
			// we're trying to zoom in
			if (pCamera->block_time > 0.5f && pSettings->fDistance > pCamera->targetdist)
			{
				// allow the user to zoom in immediately
				pSettings->fDistance = pCamera->targetdist;
			}
		}

		pSettings->fDistance = CLAMP(pSettings->fDistance + (iDelta*fScale), 0.0f, fMaxDistance);

		if (iDelta < 0) 
		{
			if (pSettings->fDistance < 2.0f)
			{
				pSettings->fDistance = 0.0f;
			}
		}
		else if (iDelta > 0)
		{
			if (pSettings->fDistance < 2.0f)
			{
				pSettings->fDistance = 2.0f;
			}
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(AutoAdjustCamDistance);
void gclCamera_AutoAdjustDistance(S32 iDirection)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		F32 fAdjustDistance = pSettings->fDefaultAdjustDistance;

		if (!nearSameF32(pSettings->fCloseAdjustDistance, pSettings->fDefaultAdjustDistance))
		{
			F32 fMaxDistance = pSettings->pfDistancePresets[2];
			F32 fCloseDistance = pSettings->pfDistancePresets[0];
			F32 fOffset = 0.0f;
		
			if (iDirection < 0)
				fOffset = -pSettings->fCloseAdjustDistance;

			if (pSettings->fDistance + fOffset <= fCloseDistance)
				fAdjustDistance = pSettings->fCloseAdjustDistance;
			else
				fAdjustDistance = pSettings->fDefaultAdjustDistance;
		}
		gclCamera_AdjustDistance(fAdjustDistance * iDirection);
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(AdjustCamPitch);
void gclCamera_AdjustPitch(int iVal)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	if (pCamera)
	{
		pCamera->campyr[0] = pCamera->targetpyr[0] = addAngle(pCamera->targetpyr[0], RAD(iVal));
	}
}

void gclCamera_SetStartingPitch(F32 fVal, bool bSet)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	if (pCamera)
	{
		if (bSet)
		{
			F32 fRads = RAD(fVal);
			pCamera->campyr[0] = pCamera->targetpyr[0] = fRads;
			g_bDoNotResetTargetPitch = true;
			g_fHeldPitch = fRads;
		}
		else
		{
			g_bDoNotResetTargetPitch = false;
		}
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(AdjustCamYaw);
void gclAdjustCameraYaw(int iVal)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	if (pCamera)
	{
		pCamera->campyr[1] = pCamera->targetpyr[1] = addAngle(pCamera->targetpyr[1], RAD(iVal));
		devassert(FINITE(pCamera->targetpyr[1]));
	}
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_HIDE ACMD_NAME(camTogglePlayerInspect);
void gclCamera_TogglePlayerInspect()
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	CameraSettings *pSettings = (pCamera ? gclCamera_GetSettings(pCamera) : NULL);

	if(pSettings)
	{
		if (gclPlayerControl_IsAlwaysUsingMouseLookForced())
		{
			pSettings->bIsInInspectMode = !pSettings->bIsInInspectMode;

			ui_GenSetGlobalStateName("FullscreenUI", pSettings->bIsInInspectMode);
		} 
		else
		{
			pSettings->bIsInInspectMode = false;

			ui_GenSetGlobalStateName("FullscreenUI", pSettings->bIsInInspectMode);
		}
	}
}

void gclCamera_SetStartingYaw(F32 fVal, bool bSet)
{
	
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	if (pCamera)
	{
		if (bSet)
		{
			F32 fRads = RAD(fVal);
			pCamera->campyr[1] = pCamera->targetpyr[1] = fRads;
			devassert(FINITE(pCamera->targetpyr[1]));
			g_bDoNotResetTargetYaw = true;
			g_fHeldYaw = fRads;
		}
		else
		{
			g_bDoNotResetTargetYaw = false;
		}
	}
}

void gclCamera_SetFocusEntityOverride(Entity *e)
{
	if (!e)
	{
		s_erFollowOverride = 0;
	}
	else
	{
		s_erFollowOverride = entGetRef(e);
	}
}


Entity* gclCamera_GetEntity(void)
{
	MovementManager*	mm;
	Entity*				e;

	if (s_erFollowOverride)
	{
		e = entFromEntityRefAnyPartition(s_erFollowOverride);
		if (e) return e;
	}
	
	if(	mmGetLocalManagerByIndex(&mm, 0) &&
		mmGetUserPointer(mm, &e))
	{
		return e;
	}
	
	return entActivePlayerPtr();
}

void gclCamera_GetFacingDirection(GfxCameraController* pCamera, bool bIgnorePitch, Vec3 vTargetDir)
{
	Mat4 xCameraMatrix;

	gfxGetActiveCameraMatrix(xCameraMatrix);

	if(g_CurrentScheme.bShowMouseLookReticle && gclPlayerControl_IsMouseLooking())
	{
		int mx, my = 0;
		Vec3 vTargetStart;

		gfxGetActiveSurfaceSize(&mx, &my);

		mx *= g_CurrentScheme.fMouseLookHardTargetX;
		my *= g_CurrentScheme.fMouseLookHardTargetY;

		editLibCursorRayEx(xCameraMatrix, mx, my, vTargetStart, vTargetDir);

		if(bIgnorePitch)
		{
			vTargetDir[1] = 0.0f;
			// renormalize
			normalVec3(vTargetDir);
		}
	}
	else
	{
		
		copyVec3(xCameraMatrix[2], vTargetDir);
		scaleVec3(vTargetDir, -1.0f, vTargetDir);
		if (bIgnorePitch)
		{
			vTargetDir[1] = 0.0f;
		}
		normalVec3(vTargetDir);
	}
}

// Get the adjusted direction required for the player to be aiming at the camera's target position
void gclCamera_GetAdjustedPlayerFacingDirection(GfxCameraController* pCamera, 
												Entity* e, 
												F32 fRange, 
												bool bIgnorePitch,
												const Vec3 vOverrideTargetDir,
												Vec3 vOffsetDir)
{
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
	Vec3 vTargetDir, vTarget, vOffset, vCamPos, vCamTargetPos;
	F32 fMinRange = pSettings->fDistance + 1.0f;
	WorldCollCollideResults wcResult;
	
	if (vOverrideTargetDir)
	{
		copyVec3(vOverrideTargetDir, vTargetDir);
		if (bIgnorePitch)
		{
			vTargetDir[1] = 0.0f;
		}
	}
	else
	{
			gclCamera_GetFacingDirection(pCamera, bIgnorePitch, vTargetDir);
	}
	copyVec3(pCamera->last_camera_matrix[3], vCamPos);
	scaleAddVec3(vTargetDir, fMinRange, vCamPos, vCamPos);
	scaleAddVec3(vTargetDir, fRange, vCamPos, vCamTargetPos);
	
	// Find the point that the camera is aiming at
	if (worldCollideRay(PARTITION_CLIENT, vCamPos, vCamTargetPos, WC_QUERY_BITS_COMBAT, &wcResult))
	{
		fRange = fMinRange + wcResult.distance;
	}
	scaleVec3(vTargetDir, fRange, vTarget);
	copyVec3(pSettings->vCurrentOffset, vOffset);
	
	if (bIgnorePitch)
	{
		vOffset[1] = 0.0f;	
	}
	else if (pSettings->fHeight > FLT_EPSILON || pSettings->fHeightMultiplier > FLT_EPSILON)
	{
		F32 fEntHeight = entGetHeightEx(e, true);
		F32 fCameraHeight = gclCamera_GetPlayerHeight(pCamera);
		vOffset[1] += (fCameraHeight - fEntHeight);
	}
	scaleByVec3(vOffset, -1.0f);
	subVec3(vTarget, vOffset, vOffsetDir);
	normalVec3(vOffsetDir);
}

// A pitch and yaw wrapper for gclCamera_GetAdjustedPlayerFacingDirection
void gclCamera_GetAdjustedPlayerFacing(GfxCameraController* pCamera, 
									   Entity* e, 
									   F32 fRange, 
									   F32* pfFaceYaw, 
									   F32* pfFacePitch)
{
	Vec3 vTargetDir, vOffsetDir, vCamPYR;
	F32 fFaceYaw = 0.0f;
	F32 fFacePitch = 0.0f;

	if (pfFaceYaw)
	{
		fFaceYaw = (*pfFaceYaw);
	}
	if (pfFacePitch)
	{
		fFacePitch = (*pfFacePitch);
	}
	setVec3(vCamPYR, fFacePitch, fFaceYaw, 0);
	createMat3_2_YP(vTargetDir, vCamPYR);

	gclCamera_GetAdjustedPlayerFacingDirection(pCamera, e, fRange, false, vTargetDir, vOffsetDir);
	
	getVec3YP(vOffsetDir, &fFaceYaw, &fFacePitch);
	if (pfFaceYaw)
	{
		(*pfFaceYaw) = fFaceYaw;
	}
	if (pfFacePitch)
	{
		(*pfFacePitch) = fFacePitch;
	}
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(CamReset) ACMD_CLIENTCMD ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_Reset(void)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	if (pCamera) 
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		Entity* pEnt = gclCamera_GetEntity();
		RegionRules* pRules = pEnt ?getRegionRulesFromEnt(pEnt) : NULL;
		
		if (g_CurrentScheme.fCamDistance > 0.0f)
		{
			g_CameraSettings.fDistance = g_CurrentScheme.fCamDistance;
		}
		else
		{
			g_CameraSettings.fDistance = g_CameraSettings.pfDistancePresets[g_CameraSettings.iCurrentPreset];
		}

		if (pEnt)
		{
			if (entCheckFlag(pEnt, ENTITYFLAG_DEAD))
			{
				pSettings->bResetCameraWhenAlive = true;
				setVec3(pCamera->targetpyr, 0,0,0);
				return;
			}
			else
			{
				F32 fTargetYaw;

				if (gclPlayerControl_IsFacingTarget())
				{
					Vec3 pyFace;
					gclPlayerControl_GetCameraSnapToFacing(pyFace);
					pSettings->fLastPlayerYaw = pyFace[1];
				}
				else if (g_CurrentScheme.bStrafing)
				{
					pSettings->fLastPlayerYaw = gclPlayerControl_GetFaceYaw();
				}
				else
				{
					Quat qRot;
					Vec3 vPYR = {0};
					entGetRot(pEnt, qRot);
					quatToPYR(qRot, vPYR);
					pSettings->fLastPlayerYaw = vPYR[1];
				}
				

				fTargetYaw = PI + pSettings->fLastPlayerYaw;
				if (fTargetYaw >= TWOPI) fTargetYaw -= TWOPI;
				pCamera->targetpyr[1] = fTargetYaw;
				devassert(FINITE(pCamera->targetpyr[1]));
			}
		}
		
		pCamera->targetpyr[0] = pSettings->fAutoLevelPitch;
		if (g_bDoNotResetTargetPitch)
		{
			pCamera->targetpyr[0] = g_fHeldPitch;
		}
		if (g_bDoNotResetTargetYaw)
		{
			pCamera->targetpyr[1] = g_fHeldYaw;
		}
		devassertmsg(FINITEVEC3(pCamera->targetpyr), "Undefined pCamera PYR!");
		
		pSettings->bCameraIsDefault = true;
		pSettings->bCameraFirstFrameAfterDefault = true;
		pSettings->bResetCameraWhenAlive = false;

		gclResetPYRInterpSpeed( pSettings );
		gclResetDistInterpSpeed( pSettings );

		gclCamera_UpdateModeForRegion(pEnt);
		gclCamera_DisableMouseLook();
	}
}

void gclCamera_DisableMouseLook(void)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		pSettings->bIsMouseLooking = false;
	}
}

// Cycle the camera distance between several preset values.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_CATEGORY(Standard, Graphics) ACMD_NAME(camCycleDist) ACMD_PRODUCTS(StarTrek, FightClub);
void gclCamera_CycleDistance(void)
{
	CameraSettings* pSettings;
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (!pCamera) return;

	pSettings = gclCamera_GetSettings(pCamera);
	
	// case of the camera distance being altered through other means 
	// find the appropriate distance to cycle to. 
	{
		S32 i;
		for (i = 0; i < CAM_DISTANCE_PRESET_COUNT; i++)
		{
			if (pSettings->fDistance < pSettings->pfDistancePresets[i])
			{
				pSettings->iCurrentPreset = i - 1;
				break;
			}
		}
		
		pSettings->iCurrentPreset = (pSettings->iCurrentPreset+1) % CAM_DISTANCE_PRESET_COUNT;
		pSettings->fDistance = pSettings->pfDistancePresets[pSettings->iCurrentPreset];
		pSettings->fAutoLevelPitch = pSettings->v3AutoLevelPitchPresets[pSettings->iCurrentPreset];
	}
}



// Vertical is from -1 to 1. -1 means fully horizontal, 1 means fully vertical, 0 means in balanced
// Pan is from 0 to 1. 0 means just do rotation, 1 means just do pan.
void gclCamera_Shake(F32 fTime, F32 fMagnitude, F32 fVertical, F32 fPan, F32 fSpeed)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

	if (pSettings->bEnableCameraShake && g_CurrentScheme.bCameraShake)
	{
		pSettings->fCameraShakeTime = fTime;
		pSettings->fCameraShakeMagnitude = fMagnitude;
		pSettings->fCameraShakeVertical = CLAMP(fVertical, -1.0f, 1.0f);
		pSettings->fCameraShakePan = CLAMP(fPan, 0.0f, 1.0f);
		pSettings->fCameraShakeSpeed = fSpeed;
	}
}

void gclCamera_ForceAutoAdjust(bool bEnable)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera)
	{	
		gclCamera_GetSettings(pCamera)->bForceAutoAdjust = bEnable;
	}
}

static void gfxCursor3d(F32 x,F32 y,F32 len,Vec3 start,Vec3 end, const Mat4 cammat)
{
	int		w,h;
	Vec3	dv;
	F32		yaw,pitch;
	Mat4	mat;
	F32		aspect;
	F32		hang,viewang,cosval,hvam,vang;

	gfxGetActiveSurfaceSize(&w,&h);
	aspect = (F32)w / (F32)h;

	vang = gfxGetActiveCameraFOV();
	hang = 2 * (fatan(aspect * ftan(vang/2.0)));
	viewang	= hang * .5;
	cosval	= cosf(viewang);
	hvam = (sinf(viewang)/cosval);
	dv[2] = 1.f / hvam;

	dv[0] = (x - w/2) / (F32)(w/2.f);
	dv[1] = (y - h/2) / (F32)(h * aspect/2.f);
	getVec3YP(dv,&yaw,&pitch);
	yawMat3(yaw,mat);
	pitchMat3(pitch,mat);

	copyVec3(mat[3],start);
	copyVec3(mat[3],end);
	moveVinZ(start,mat,-1.f);
	moveVinZ(end,mat,-len);
}

static void gfxGetCameraPlaneDimensions(F32 *pWidth, F32 *pHeight)
{
	int		w,h;
	F32		aspect;
	F32		vang, nearz;
	F32		tanhalfy;

	gfxGetActiveSurfaceSize(&w,&h);
	aspect = (F32)w / (F32)h;

	vang = gfxGetActiveCameraFOV();

	nearz = gfxGetActiveCameraNearPlaneDist();
	nearz = ABS(nearz);
	tanhalfy = tanf(RAD(vang*0.5f));

	*pHeight = nearz * tanhalfy * 2.0f;
	*pWidth = *pHeight*aspect;
}

// Basic check to see if the same thing is covering ~1/2 of the screen
//
static int gfxCheckLargeScreenOcclude(F32 dist, const Mat4 pCamera)  
{
	WorldCollCollideResults colls[4];
	WorldCollCollideResults middle;
	Vec3 start, end;
	U32 w,h;
	int x,y,i;

	gfxGetActiveSurfaceSize(&w,&h);
	gfxCursor3d(w/2,h/2,dist,start,end,pCamera);

	if(!worldCollideRay(PARTITION_CLIENT, start, end, WC_QUERY_BITS_TARGETING, &middle ))
		return 0;

	for( x = 0; x < 2; x++ )
	{
		for( y = 0; y < 2; y++ )
		{
			gfxCursor3d(x*w,y*h,dist,start,end,pCamera);
			worldCollideRay(PARTITION_CLIENT, start, end, WC_QUERY_BITS_TARGETING, &colls[x+2*y]);
		}
	}

	for( i = 0; i < 4; i++ )
	{
		if( middle.node == colls[i].node )
			return 1;
	} 
	return 0;
}

static bool gclCamera_IsEntCrouching(CameraSettings* pSettings, Entity* pEnt)
{
	if (pEnt && entIsCrouching(pEnt))
	{
		Vec3 vVel;
		entCopyVelocityFG(pEnt, vVel); 
		return (lengthVec3SquaredXZ(vVel) < 1);
	}
	else if (pEnt && pSettings->ppchCrouchActions)
	{
		// This is an ABOMINATION
		if (!gConf.bNewAnimationSystem)
		{
			S32 i;
			DynSkeleton* pSkel = dynSkeletonFromGuid(pEnt->dyn.guidSkeleton);
			DynSequencer* pSqr = pSkel ? eaGet(&pSkel->eaSqr,0) : NULL;
			const char* pchAction = pSqr ? dynSeqGetCurrentActionName(pSqr) : NULL;

			for (i = eaSize(&pSettings->ppchCrouchActions)-1; i >= 0; i--)
			{
				if (pSettings->ppchCrouchActions[i] == pchAction)
				{
					return true;
				}
			}
		}
	}
	return false;
}

static void gclCamera_UpdateHeightMultiplier(Entity* pEnt, GfxCameraController* pCamera, F32 fElapsedTime)
{
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
	const F32 fRate = pSettings->fHeightAdjustRate;
	F32 fTargetHeightMult;
	static F32 fMountHeight;

	if (pSettings->eMode == kCameraMode_GiganticCamera)
	{
		GiganticCameraSettings* pGiganticSettings = (GiganticCameraSettings*)(pSettings->pModeSettings);
		F32 fDistScale = gclCamera_GetDistanceScale(pSettings);
		F32 fPlayerHeightMult = pSettings->fPlayerHeightMultiplier;
		F32 fGiganticHeightMult = FIRST_IF_SET(pGiganticSettings->fHeightMult, fPlayerHeightMult);
		F32 fInterpDistMax = 0.0f;

		if (pGiganticSettings->fMinDistMult < FLT_EPSILON)
		{
			fInterpDistMax = pSettings->fPlayerFadeDistance * fDistScale;
		}
		if (pCamera->camdist > fInterpDistMax)
		{
			if (!pGiganticSettings->bHeightInit)
			{
				F32 fCurrentHeight = gclCamera_GetPlayerHeight(pCamera);
				F32 fLastHeight = pCamera->camfocus[1] - pCamera->camcenter[1];

				if (fCurrentHeight > FLT_EPSILON && pGiganticSettings->fHeightAdjustTime > FLT_EPSILON)
				{
					F32 fHeightMultDiff;
					pSettings->fHeightMultiplier *= (fLastHeight/fCurrentHeight);
					fHeightMultDiff = MAXF(fGiganticHeightMult - pSettings->fHeightMultiplier, 0.01f);
					pGiganticSettings->fHeightAdjustRate = fHeightMultDiff / pGiganticSettings->fHeightAdjustTime;
				}
				else
				{
					pSettings->fHeightMultiplier = fGiganticHeightMult;
				}
				pGiganticSettings->bHeightInit = true;
			}

			if (pSettings->fHeightMultiplier >= fGiganticHeightMult)
			{
				pSettings->fHeightMultiplier = fGiganticHeightMult;
			}
			else
			{
				pSettings->fHeightMultiplier += (pGiganticSettings->fHeightAdjustRate * fElapsedTime);
				MIN1F(pSettings->fHeightMultiplier, fGiganticHeightMult);
			}
		}
		else
		{
			F32 fInterpScale = pCamera->camdist / fInterpDistMax;
			F32 fInterp = interpF32(fInterpScale, fPlayerHeightMult, fGiganticHeightMult);
			pSettings->fHeightMultiplier = fInterp;
		}
		return;
	}

	if (SAFE_MEMBER2(pEnt, pPlayer, InteractStatus.bSittingInChair))
	{
		fTargetHeightMult = pSettings->fSitHeightMult;
	}
	else if (gclCamera_IsEntCrouching(pSettings, pEnt))
	{
		fTargetHeightMult = pSettings->fCrouchHeightMult;
	}
	else if (pSettings->bIsInAimMode && pSettings->fAimHeightMult > FLT_EPSILON)
	{
		fTargetHeightMult = pSettings->fAimHeightMult;
	}
	else if (pSettings->bIsInInspectMode)
	{
		F32 fPlayerHeight = entGetHeight(pEnt);
		F32 fMaxDist = (pSettings->eMode == kCameraMode_ShooterCamera ? ((ShooterCameraSettings *)(pSettings->pModeSettings))->fMaxDist : pSettings->fMaxDistanceMaxValue);
		F32 fZoomVal = pSettings->fDistance / fMaxDist;
		F32 fTemp1 = 0;
		F32 fTemp2 = 0;

		fTemp1 = lerp(fPlayerHeight / pSettings->fHeight, 0, fZoomVal);
		fTemp2 = lerp(0, pSettings->fPlayerHeightMultiplier, fZoomVal);
		fTargetHeightMult = lerp(fTemp1, fTemp2, fZoomVal);
	}
	else if(pEnt->costumeRef.pMountCostume)
	{
		if(!fMountHeight)
		{
			// cache mount height as soon as mounted.  This is so the camera isn't affected by dynamic changes to the skeleton height, like when a horse rears up.
			DynSkeleton *pSkeleton = dynSkeletonFromGuid(pEnt->dyn.guidSkeleton);
				//this is a pretty lame approximation; the goal is to see your target over your horse's head so it uses height and extent forward.
			fMountHeight = pSkeleton->vCurrentExtentsMax[1] - pSkeleton->vCurrentExtentsMin[1] + pSkeleton->vCurrentExtentsMax[2] / 2;
		}
		fTargetHeightMult = pSettings->fMountedHeightMultiplier * (fMountHeight / 6.0F);	//6.0 is expected character height.
	}
	else
	{
		fTargetHeightMult = pSettings->fPlayerHeightMultiplier;
	}

	if(fMountHeight && !pEnt->costumeRef.pMountCostume)
	{
		//clear cached mount height.
		fMountHeight = 0;
	}

	if (fTargetHeightMult > pSettings->fHeightMultiplier)
	{
		pSettings->fHeightMultiplier = MINF(pSettings->fHeightMultiplier+fRate*fElapsedTime, fTargetHeightMult); 
	}
	if (fTargetHeightMult < pSettings->fHeightMultiplier)
	{
		pSettings->fHeightMultiplier = MAXF(pSettings->fHeightMultiplier-fRate*fElapsedTime, fTargetHeightMult); 
	}
}

static F32 gclCamera_GetFOVy(SA_PARAM_NN_VALID GfxCameraController* pCamera)
{
	F32 fFOV = RAD(pCamera->projection_fov);
	if (pCamera->useHorizontalFOV)
	{
		fFOV = 2.0f * atan(tan(fFOV * 0.5f) / gfxGetAspectRatio());
	}
	return fFOV;
}

static F32 gclCamera_GetClosestDistance(CameraSettings *pSettings)
{
	F32 fDistance = pSettings->fClosestDistance;
	switch (pSettings->eMode)
	{
		xcase kCameraMode_AimCamera:
		{
			AimCameraSettings* pAimSettings = (AimCameraSettings*)(pSettings->pModeSettings);
			if (!pAimSettings->bUseClosestDistanceInShooterOnly || g_CurrentScheme.bEnableShooterCamera)
			{
				fDistance = pAimSettings->fClosestDistance;
			}
		}
		xcase kCameraMode_ShooterCamera:
		{
			ShooterCameraSettings* pShooterSettings = (ShooterCameraSettings*)(pSettings->pModeSettings);
			fDistance = pShooterSettings->fClosestDistance;
		}
		xcase kCameraMode_GiganticCamera:
		{
			GiganticCameraSettings* pGiganticSettings = (GiganticCameraSettings*)(pSettings->pModeSettings);
			Entity *pEnt = gclCamera_GetEntity();
			F32 fPlayerHeight = entGetHeight(pEnt);
			fDistance = fPlayerHeight * pGiganticSettings->fMinDistMult; 
		}
	}
	fDistance *= pSettings->fZoomMult;

	if(pSettings->bIsInInspectMode)
		fDistance = 0;

	return fDistance;
}

static F32 gclCamera_GetFlightZoomHeightOffet(GfxCameraController* pCamera, F32 fElapsed)
{
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
	F32 fPlayerHeight = gclCamera_GetPlayerHeight(pCamera);
	F32 fSlope = tan(gclCamera_GetFOVy(pCamera) * 0.5f) * 0.35f;
	F32 fOffset = fSlope * pCamera->camdist;
	const F32 fOffsetSpeed = 2.0f;

	if (fElapsed > FLT_EPSILON)
	{
		F32 fClosestDist = gclCamera_GetClosestDistance(pSettings);

		if (pCamera->camdist < fClosestDist + 0.5f)
		{
			if (pSettings->fHeightOffsetMult > 0.0f)
				pSettings->fHeightOffsetMult -= fOffsetSpeed*fElapsed;
			MAX1F(pSettings->fHeightOffsetMult, 0.0f);
		}
		else if (pCamera->camdist > fClosestDist + 1.0f)
		{
			if (pSettings->fHeightOffsetMult < 1.0f)
				pSettings->fHeightOffsetMult += fOffsetSpeed*fElapsed;
			MIN1F(pSettings->fHeightOffsetMult, 1.0f);
		}
	}
	fOffset *= pSettings->fHeightOffsetMult;
	
	if (fOffset > 0.0f)
	{
		F32 fPitchAbs;
		Vec3 vDir;
		subVec3(pCamera->last_camera_matrix[3], pCamera->camcenter, vDir);
		fPitchAbs = ABS(getVec3Pitch(vDir));
		fOffset *= cosf(fPitchAbs);
	}
	return fOffset;
}

F32 gclCamera_GetPlayerHeight(GfxCameraController* pCamera)
{
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
	F32 fHeightMultiplier = 1.0f;
	Entity* pEnt;

	fHeightMultiplier = pSettings->fHeightMultiplier;

	if (pSettings->fHeight)
	{
		return pSettings->fHeight * fHeightMultiplier;
	}

	pEnt = gclCamera_GetEntity();
	if (pEnt)
	{
		return entGetHeight(pEnt) * fHeightMultiplier;
	}

	return 6.0f * fHeightMultiplier;
}

F32 gclCamera_GetKeyboardYawTurnSpeed()
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();
	if ( pCamera )
	{
		CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
		return pSettings->pyrKeyboardSpeed[1];
	}

	return 0.f;
}

static bool gclCamera_IsPlaneObstructed(const Mat4 xCameraMatrix, const Vec3 vEntPos, F32 fTargetDist)
{
	WorldCollCollideResults wcResult;
	Vec3 vCastSt, vCastEnd, vPlanePos;
	F32	camPlaneWidth, camPlaneHeight;
	F32 nearPlane = gfxGetActiveCameraNearPlaneDist();
	
	copyVec3(xCameraMatrix[3], vCastSt);
	// cast from the camera position to the lower left camera plane

	gfxGetCameraPlaneDimensions(&camPlaneWidth, &camPlaneHeight);

	copyVec3(vEntPos, vPlanePos);
	moveVinZ(vPlanePos, xCameraMatrix, fTargetDist);
	scaleAddVec3(xCameraMatrix[2], nearPlane, vPlanePos, vPlanePos);

	//scaleAddVec3(xCameraMatrix[2], -nearPlane, vCamPos, vCastSt);
	scaleAddVec3(xCameraMatrix[0], camPlaneWidth, vPlanePos, vCastSt);
	scaleAddVec3(xCameraMatrix[1], -camPlaneHeight, vCastSt, vCastSt);

	//scaleAddVec3(xCameraMatrix[2], -nearPlane, vCamPos, vCastEnd);
	scaleAddVec3(xCameraMatrix[0], -camPlaneWidth, vPlanePos, vCastEnd);
	scaleAddVec3(xCameraMatrix[1], camPlaneHeight, vCastEnd, vCastEnd);
	if (worldCollideRay(PARTITION_CLIENT, vCastSt, vCastEnd, WC_QUERY_BITS_CAMERA_BLOCKING, &wcResult) ||
		worldCollideRay(PARTITION_CLIENT, vCastEnd, vCastSt, WC_QUERY_BITS_CAMERA_BLOCKING, &wcResult) )
	{
		return true;
	}

	//scaleAddVec3(xCameraMatrix[2], -nearPlane, vCamPos, vCastSt);
	scaleAddVec3(xCameraMatrix[0], camPlaneWidth, vPlanePos, vCastSt);
	scaleAddVec3(xCameraMatrix[1], camPlaneHeight, vCastSt, vCastSt);

	//scaleAddVec3(xCameraMatrix[2], -nearPlane, vCamPos, vCastEnd);
	scaleAddVec3(xCameraMatrix[0], -camPlaneWidth, vPlanePos, vCastEnd);
	scaleAddVec3(xCameraMatrix[1], -camPlaneHeight, vCastEnd, vCastEnd);
	if (worldCollideRay(PARTITION_CLIENT, vCastSt, vCastEnd, WC_QUERY_BITS_CAMERA_BLOCKING, &wcResult) ||
		worldCollideRay(PARTITION_CLIENT, vCastEnd, vCastSt, WC_QUERY_BITS_CAMERA_BLOCKING, &wcResult) )
	{
		return true;
	}

	return false;
}

void gclCamera_GetTargetPosition(GfxCameraController *pCamera, Vec3 vEntPos)
{
	CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
	Entity *pEnt = gclCamera_GetEntity();

	// Make sure that the player camera height doesn't put us through a ceiling
	if (pEnt)
	{
		WorldCollCollideResults wcResult = {0};
		Vec3 vCamEntPos;
		F32 fCamPlayerHeight = gclCamera_GetPlayerHeight(pCamera);

		entGetPos(pEnt, vEntPos);
		copyVec3(vEntPos, vCamEntPos);
		vCamEntPos[1] += fCamPlayerHeight + pSettings->fTargetRadius;

		// ray cast from a point near the top of our body to a point above our head, to look for ceilings
		vEntPos[1] += fCamPlayerHeight*0.75f;

		if (worldCollideRay(PARTITION_CLIENT, vEntPos, vCamEntPos, WC_QUERY_BITS_CAMERA_BLOCKING, &wcResult))
		{
			copyVec3(wcResult.posWorldImpact, vEntPos);
			vEntPos[1] -= pSettings->fTargetRadius + 0.1f;
		}
		else
		{ // did not hit a ceiling
			entGetPos(pEnt, vEntPos);
			vEntPos[1] += fCamPlayerHeight;
		}
	}
	else
	{
		copyVec3(pCamera->camcenter, vEntPos);
	}
}

F32	gclCamera_GetMouseSensitivityMin(void)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		return pSettings->fMouseSensitivityMin;
	}
	return 0.0f;
}

F32	gclCamera_GetMouseSensitivityMax(void)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	if (pCamera)
	{
		CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
		return pSettings->fMouseSensitivityMax;
	}
	return 0.0f;
}

static int gclCamera_ClientOverridenWorldDrawableEntryCompare(const ClientOverridenWorldDrawableEntry *pCurrentEntry, const ClientOverridenWorldDrawableEntry *pComparedEntry)
{
	return pCurrentEntry && pComparedEntry && pComparedEntry->pDrawableEntry == pCurrentEntry->pDrawableEntry;
}

static void gclCamera_RestoreAlphaOfOverriddenDrawables(SA_PARAM_NN_VALID ClientOverridenWorldDrawableEntry ***pppOverridenEntriesThisCall)
{
	if (pppOverridenEntriesThisCall)
	{
		FOR_EACH_IN_EARRAY(g_ppClientOverridenWorldDrawableEntries, ClientOverridenWorldDrawableEntry, pOverriddenEntry)
		{
			if (pOverriddenEntry)
			{
				// See if this overridden entry is still obstructing the camera
				bool bStillObstructsCamera = eaFindCmp(pppOverridenEntriesThisCall, pOverriddenEntry, gclCamera_ClientOverridenWorldDrawableEntryCompare) >= 0;

				if (!bStillObstructsCamera)
				{
					// Restore the alpha and erase
					if (pOverriddenEntry->pDrawableEntry)
					{
						pOverriddenEntry->pDrawableEntry->color[3] = pOverriddenEntry->fOriginalAlpha;
						pOverriddenEntry->pDrawableEntry->is_camera_fading = 0;
					}
					eaRemove(&g_ppClientOverridenWorldDrawableEntries, FOR_EACH_IDX(g_ppClientOverridenWorldDrawableEntries, pOverriddenEntry));

					// Free up the memory
					StructDestroy(parse_ClientOverridenWorldDrawableEntry, pOverriddenEntry);
				}
			}
		}
		FOR_EACH_END

		// Add all overridden entries from this call to the global list
		FOR_EACH_IN_EARRAY(*pppOverridenEntriesThisCall, ClientOverridenWorldDrawableEntry, pOverriddenEntryThisCall)
		{
			if (pOverriddenEntryThisCall && eaFindCmp(&g_ppClientOverridenWorldDrawableEntries, pOverriddenEntryThisCall, gclCamera_ClientOverridenWorldDrawableEntryCompare) < 0) 
			{
				ClientOverridenWorldDrawableEntry *pOverriddenEntryCopy = StructClone(parse_ClientOverridenWorldDrawableEntry, pOverriddenEntryThisCall);
				pOverriddenEntryCopy->pDrawableEntry = pOverriddenEntryThisCall->pDrawableEntry;
				eaPush(&g_ppClientOverridenWorldDrawableEntries, pOverriddenEntryCopy);
			}
		}
		FOR_EACH_END
	}
}

static void gclCamera_SetAlphaOfCameraObstructingDrawable(SA_PARAM_NN_VALID WorldDrawableEntry * pWorldDrawable, 
															 SA_PARAM_NN_VALID ClientOverridenWorldDrawableEntry ***pppOverridenEntriesThisCall, 
															 SA_PARAM_NN_VALID S32 *pEntryCount,
															 bool bVanish)
{
	if (pWorldDrawable && 
		pEntryCount && 
		pppOverridenEntriesThisCall)
	{
		ClientOverridenWorldDrawableEntry *pNewOverriddenEntry = NULL;

		// Make sure this is not already updated within this call
		FOR_EACH_IN_EARRAY_FORWARDS(*pppOverridenEntriesThisCall, ClientOverridenWorldDrawableEntry, pOverriddenEntry)
		{
			if (FOR_EACH_IDX(*pppOverridenEntriesThisCall, pOverriddenEntry) >= *pEntryCount)
			{
				break;
			}

			if (pOverriddenEntry &&
				pOverriddenEntry->pDrawableEntry == pWorldDrawable)
			{
				return;
			}
		}
		FOR_EACH_END

		pNewOverriddenEntry = eaGetStruct(pppOverridenEntriesThisCall, parse_ClientOverridenWorldDrawableEntry, (*pEntryCount)++);
		pNewOverriddenEntry->pDrawableEntry = pWorldDrawable;
		pNewOverriddenEntry->fOriginalAlpha = pWorldDrawable->color[3];

		if (bVanish)
		{
			pWorldDrawable->color[3] = MAX(pWorldDrawable->color[3]-0.08f,0.0f);
		}
		else
		{
			pWorldDrawable->color[3] = MINF(CAMERA_OBSTRUCTING_OBJECT_ALPHA,  pWorldDrawable->color[3]);	
		}
		pWorldDrawable->is_camera_fading = 1;
	}
}

typedef struct CameraRayCollideCBData {
	U32										count;
	ClientOverridenWorldDrawableEntry***	pppModifiedEntriesThisCall;
	S32*									piModifiedEntryCount;
	Vec3									vCamPos;
} CameraRayCollideCBData;

static S32 cameraRayCollideResultsCB(	CameraRayCollideCBData* cbData,
										const WorldCollCollideResults* results)
{
	WorldCollisionEntry *pCollEntry = NULL;

	if (wcoGetUserPointer(results->wco, entryCollObjectMsgHandler, &pCollEntry))
	{
		GroupTracker *tracker;
		U32 filterBits;
		bool bVanish;
		psdkActorGetFilterBits(	results->psdkActor, &filterBits);
		bVanish = !!(filterBits & WC_FILTER_BIT_CAMERA_VANISH);

		// This will probably need to be made more sophisticated - for now, only vanish objects that are within 10 units of the camera
		if (bVanish && distance3(results->posWorldImpact,cbData->vCamPos) > 10.0f)
		{
			return 1;
		}


		tracker = trackerFromTrackerHandle(SAFE_MEMBER(pCollEntry, tracker_handle));
		if (tracker)
		{
			EARRAY_CONST_FOREACH_BEGIN(tracker->cell_entries, i, isize);
			{
				WorldCellEntry *pCellEntry = tracker->cell_entries[i];
				if (pCellEntry->type > WCENT_BEGIN_DRAWABLES)
				{
					gclCamera_SetAlphaOfCameraObstructingDrawable(	(WorldDrawableEntry *)pCellEntry,
																	cbData->pppModifiedEntriesThisCall,
																	cbData->piModifiedEntryCount,
																	bVanish);
				}
			}
			EARRAY_FOREACH_END;
		}
		else if (pCollEntry)
		{
			// when no trackers are available (i.e. not in edit/view layer mode), search world cell 
			// tree for corresponding drawable model entry
			WorldDrawableEntry * pWorldDrawable = worldCollisionEntryToWorldDrawable(pCollEntry);
			if (pWorldDrawable)
			{
				gclCamera_SetAlphaOfCameraObstructingDrawable(	pWorldDrawable,
																cbData->pppModifiedEntriesThisCall,
																cbData->piModifiedEntryCount,
																bVanish);
			}
		}
	}

	return ++cbData->count < 10;
}

// Just returns the best z distance, without modifying anything
F32 gclCamera_GetZDistanceFromTargetEx(GfxCameraController *pCamera, const Mat4 xCameraMatrix, Vec3 vEntPos, U32* piRayHitCount)
{
	CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
	S32 i;
	F32 fOffset, fResult = -1.0f;
	Vec3 vCamPos;

	static F32 sfInner = 0.7f;
	
	// [-1,1]
	Vec2 avRayCastPoints[] = { {-1.f,-1.f},
								{1.f,-1.f},
								{-1.f,1.f},
								{1.f,1.f},
								{0.f,-sfInner},
								{0.f,sfInner},
								{-sfInner,0.f},
								{sfInner,0.f}
	};

	// sorted list of distances we hit at.  If we suddenly cross the threshold, we don't want to spaz out and return a distance that is way closer
	F32 afDistances[MAX_IGNORABLE_RAYCAST_HITS+1];
	int iRaycastHits=0;
	int iRaycastTests = sizeof(avRayCastPoints)/sizeof(Vec2);

	// The list of modified drawable entries this round
	static ClientOverridenWorldDrawableEntry **ppModifiedEntriesThisCall = NULL;

	S32 iModifiedEntryCount = 0;

	//this works, as long as the camera is always looking at the entity
	copyVec3(vEntPos, vCamPos);
	moveVinZ(vCamPos, xCameraMatrix, pSettings->fDistance + 1.0f);

	for (i = 0; i < iRaycastTests; i++)
	{
		Vec3 vCamSrc, vCamDst;
		// fake the compiler out so it doesn't warn about uninitialized mem
		WorldCollCollideResults wcResult = {0};
		CameraRayCollideCBData cbData = {0};

		copyVec3(vEntPos, vCamDst);
		fOffset = pSettings->fTargetRadius*avRayCastPoints[i][1];
		moveVinY(vCamDst, xCameraMatrix, fOffset);
		fOffset = pSettings->fTargetRadius*avRayCastPoints[i][0];
		moveVinX(vCamDst, xCameraMatrix, fOffset);

		// not using fRadius for now. The radius works best when less than fTargetRadius
		// that is hard coded above. 
		copyVec3(vCamPos, vCamSrc);
		fOffset = 0.5f * pSettings->fTargetRadius*avRayCastPoints[i][1];
		moveVinY(vCamSrc, xCameraMatrix, fOffset);
		fOffset = 0.5f * pSettings->fTargetRadius*avRayCastPoints[i][0];
		moveVinX(vCamSrc, xCameraMatrix, fOffset);

		copyVec3(vCamPos,cbData.vCamPos);

		if (worldCollideRay(PARTITION_CLIENT, vCamDst, vCamSrc, WC_QUERY_BITS_CAMERA_BLOCKING, &wcResult))
		{
			int iSlot;
			int iDistancesStored = MIN(iRaycastHits,MAX_IGNORABLE_RAYCAST_HITS+1);
			for (iSlot=0;iSlot<iDistancesStored;iSlot++)
			{
				if (afDistances[iSlot] > wcResult.distance)
				{
					int jSlot;
					int iMaxSlots = MIN(iRaycastHits,MAX_IGNORABLE_RAYCAST_HITS);
					for (jSlot=iMaxSlots-1;jSlot>=iSlot;jSlot--)
					{
						afDistances[jSlot+1] = afDistances[jSlot];
					}
					break;
				}
			}

			if (iSlot < MAX_IGNORABLE_RAYCAST_HITS+1)
			{
				// calculate the correct distance to get this thing out of the way
				Vec3 vToCam;
				F32 fDot = -dotVec3(wcResult.normalWorld,xCameraMatrix[2]);

				// this will be our default - it's aggressive in some situations, and won't work in others
				afDistances[iSlot] = wcResult.distance;

				subVec3(vCamPos,vCamSrc,vToCam);
				if (fDot > 0.0f)
				{
					// theoretically, the camera can be moved forward to get this out of the way
					F32 fDistanceToSurface,fMoveDist;
					Vec3 vToSurface;
					subVec3(wcResult.posWorldImpact,vCamSrc,vToSurface);
					fDistanceToSurface = dotVec3(vToSurface,wcResult.normalWorld);
					fMoveDist = fDistanceToSurface/fDot;
					if (fMoveDist < pSettings->fDistance+1.0f)
					{
						afDistances[iSlot] = pSettings->fDistance-fMoveDist;
					}
				}
			}

			iRaycastHits++;
		}

		// the first 4 will be adequate
		if (i < 4)
		{
			// Do a ray cast for things set to not collide with the camera as well, so we can fade them out (This doesn't REALLY belong in this function)
			cbData.pppModifiedEntriesThisCall = &ppModifiedEntriesThisCall;
			cbData.piModifiedEntryCount = &iModifiedEntryCount;
			worldCollideRayMultiResult(	PARTITION_CLIENT,
									vCamDst,
									vCamSrc,
									WC_FILTER_BIT_CAMERA_FADE | WC_FILTER_BIT_CAMERA_VANISH,
									cameraRayCollideResultsCB,
									&cbData);

			worldCollideRayMultiResult(	PARTITION_CLIENT,
									vCamSrc,
									vCamDst,
									WC_FILTER_BIT_CAMERA_FADE | WC_FILTER_BIT_CAMERA_VANISH,
									cameraRayCollideResultsCB,
									&cbData);
		}
	}

	fResult = afDistances[MIN(iRaycastHits-1,MAX_IGNORABLE_RAYCAST_HITS)];

	if (piRayHitCount)
	{
		*piRayHitCount = iRaycastHits;
	}

	// Clean up unused structs
	for (i = iModifiedEntryCount; i < eaSize(&ppModifiedEntriesThisCall); i++)
	{		
		StructDestroy(parse_ClientOverridenWorldDrawableEntry, ppModifiedEntriesThisCall[i]);
	}
	// Trim the list
	eaSetSize(&ppModifiedEntriesThisCall, iModifiedEntryCount);

	// Restore all overridden entries which do not exist in the new list
	gclCamera_RestoreAlphaOfOverriddenDrawables(&ppModifiedEntriesThisCall);

	return fResult;
}

F32 gclCamera_GetZDistanceFromTarget(GfxCameraController *pCamera, const Mat4 xCameraMatrix)
{
	Vec3 vEntPos;
	gclCamera_GetTargetPosition(pCamera, vEntPos);
	return gclCamera_GetZDistanceFromTargetEx(pCamera, xCameraMatrix, vEntPos, NULL);
}

// Determines target distance, and interpolates actual distance
void gclCamera_FindProperZDistance(GfxCameraController *pCamera, const Mat4 xCameraMatrix, F32 fDTime, F32 fDistScale, F32 fDefaultDist)
{
	CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
	F32 fNewTargetDist;
	static F32 s_fCameraResetDelayTimer = -1.f;
	F32 fMinZDistance, fDistance = fDefaultDist >= 0.0f ? fDefaultDist : pSettings->fDistance;
	bool bSnapToMinDistance = false;
	U32 uRayHitCount = 0;
	Vec3 vEntPos;

	//if we're zoomed in all the way, this function isn't necessary
	if (fDistance < 0.01f && pCamera->camdist < 0.01f) 
	{
		return;
	}
	
	if (s_bDisableCameraCollision)
	{
		pCamera->camdist = fDistance;
		return;
	}

	fNewTargetDist = fDistance;
	gclCamera_GetTargetPosition(pCamera, vEntPos);
	fMinZDistance = gclCamera_GetZDistanceFromTargetEx(pCamera, xCameraMatrix, vEntPos, &uRayHitCount);

	if (uRayHitCount == 0)
	{
		fMinZDistance = fDistance;
	}
	else if (fMinZDistance < 0.0f)
	{
		fMinZDistance = 0.01f;
	}

	// throw rays from player back to camera in camera shape
	if (uRayHitCount > MAX_IGNORABLE_RAYCAST_HITS)
	{
		if (uRayHitCount >= 6)
		{
			pCamera->block_time = 1.f;
		}	
		else
		{
			pCamera->block_time += fDTime;
		}

		if (pCamera->block_time >= 1.f)
		{	// after a second of being blocked, bring the camera in
			bSnapToMinDistance = true;
		}
	}
	else
	{
		pCamera->block_time = 0.f;

		// this is a judgement call, but under the notion that this feature is designed to prevent the camera bouncing as it is dragged along rough surfaces,
		// it is not necessary to do this delay once the camera is no longer blocked.
		s_fCameraResetDelayTimer = -1.0f;
	}

	if (!bSnapToMinDistance)
	{
		if (gclCamera_IsPlaneObstructed(xCameraMatrix, vEntPos, fNewTargetDist))
		{
			bSnapToMinDistance = true;
		}
	}

	if (bSnapToMinDistance)
	{
		fNewTargetDist = MAX(fMinZDistance, 1.f);
	}
	
	if (fDistScale >= 0.0f)
	{
		fNewTargetDist *= fDistScale;
	}
	
	if(bSnapToMinDistance)
	{
		// we calculate more accurate distances now, but we still throw the first 3 out, to avoid popping the camera forward,
		// so I'm leaving this 0.8 tolerance on here.  It might be better to do some sort of distance threshold before throwing those out. [RMARR - 5/4/11]
		pCamera->targetdist = fNewTargetDist - 0.8f;
		if (pCamera->targetdist <= pCamera->camdist)
		{
			// refresh the reset delay timer as long as we are blocked
			s_fCameraResetDelayTimer = s_fCameraDistResetTime;
		}
	}
	else
	{
		pCamera->targetdist = fNewTargetDist;
	}

	if (bSnapToMinDistance && pCamera->targetdist < pCamera->camdist)
	{
		pCamera->camdist = pCamera->targetdist;
	}
	else
	{
		F32 fEffectiveDTime = fDTime;
		F32 fDistDiff = pCamera->targetdist - pCamera->camdist;
		F32 fDistAbs = ABS(fDistDiff);

		if (s_fCameraResetDelayTimer > 0.f)
		{
			// wait for an amount of time to pass before we try to return to the target dist
			s_fCameraResetDelayTimer -= fDTime;

			// apply a drag to the camera interp out
			fEffectiveDTime *= 1.0f - (s_fCameraResetDelayTimer/s_fCameraDistResetTime);
		}

		if (fDistAbs > 0.001f)
		{
			F32 fDeltaDist = pSettings->fDistanceInterpSpeed * fEffectiveDTime;

			if (fDeltaDist > fDistAbs)
			{
				fDeltaDist = fDistDiff;
			} 
			else if (fDistDiff < 0.f)
			{
				fDeltaDist = -fDeltaDist;
			}
				
			pCamera->camdist += fDeltaDist;
		}
	}
}

// This function is required in order to make sure that the camera is not clipping geometry from above the
// character. It serves the same purpose as gclCamera_FindProperZDistance, however the eye point on the character 
// is high up enough that it creates problems that gclCamera_FindProperZDistance cannot solve easily.
static void gclCamera_PushAwayFromCeiling(GfxCameraController *pCamera, Vec3 vTargetCamPos, F32 fElapsedTime)
{
	WorldCollCollideResults wcResult;
	Vec3 vCastSt, vCastEnd;
	F32	camPlaneHeight, camPlaneWidth;

	gfxGetCameraPlaneDimensions(&camPlaneWidth, &camPlaneHeight);

	copyVec3(vTargetCamPos, vCastSt);
	vCastSt[1] -= camPlaneHeight;
	copyVec3(vTargetCamPos, vCastEnd);
	vCastEnd[1] += camPlaneHeight;

	if (worldCollideRay(PARTITION_CLIENT, vCastSt, vCastEnd, WC_QUERY_BITS_CAMERA_BLOCKING, &wcResult))
	{
		vTargetCamPos[1] -= 2.f * camPlaneHeight - wcResult.distance;
	}
}

/// HACK ALERT:
///
/// This function is a near-copy of gclCamera_FindProperZDistance.  The
/// major difference is that it just modifies xCameraMatrix instead of
/// setting camera controller data, because the demo does not use the
/// same camera-controller logic that normal playback does.
void gclCamera_ApplyProperZDistanceForDemo(Mat4 xCameraMatrix, bool is_absolute)
{
	F32 cameraRadius = 1.0f;  // cushion radius around camera
	F32 dist, targetDist;
	Vec3 camPos, camTarget;//, dv;
	F32 zoomTime = 0.6f;
	int i, j, iHitWorld=0;
	bool bHitTerrain=0;
	bool bHitWorld=0;
	static F32 count = 0;
	WorldCollCollideResults wcResults[9] = {0};
	F32 dx, dy;
	Entity *pEnt = entActivePlayerPtr();
	Vec3 entPos;
	Vec3 camToEntVec;
	Mat4 camToEntMat;
	GfxCameraController *pCamera = gfxGetActiveCameraController();
	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );

	if (!demo_playingBack() || !pEnt || is_absolute)
		return;

	gamepadGetRightStick( &dx, &dy );

	//camera_values.bPlayerIsBlocked = false;
	pSettings->bPlayerIsBlocked = false;
	
	// Now Pull the camera as far back as it will go.
	entGetPos(pEnt, entPos);
	assert(FINITEVEC3(entPos));
	entPos[1] += gclCamera_GetPlayerHeight(pCamera);
	copyVec3(entPos, camTarget);
	camTarget[1] += pSettings->fCameraCenterOffset;

	//gfxDrawLine3D(camTarget, entPos, ARGBToColor(0xffffffff));
	
	// get the vec from camera to player (instead of camera to target)
	copyVec3(entPos, camPos);
	subVec3(xCameraMatrix[3], camPos, camToEntVec);
	orientMat3(camToEntMat, camToEntVec);

	targetDist = dist = lengthVec3( camToEntVec );
	moveVinZ(camPos, camToEntMat, dist+1.0);
	assert(FINITEVEC3(camPos));

	// throw rays from player back to camera in camera shape
	for( i = 0; i < 3; i++ )
	{
		for( j = 0; j < 3; j++ )
		{
			Vec3 camSrc, camDst;

			copyVec3(entPos, camDst);
			moveVinY(camDst, camToEntMat, cameraRadius*(i-1));
			moveVinX(camDst, camToEntMat, cameraRadius*(j-1));

			copyVec3(camPos, camSrc);
			moveVinY(camSrc, camToEntMat, cameraRadius*(i-1));
			moveVinX(camSrc, camToEntMat, cameraRadius*(j-1));

			//gfxDrawLine3D(camSrc, camDst, ARGBToColor(0xffff0000));

			bHitWorld |= worldCollideRay(PARTITION_CLIENT, camDst, camSrc, WC_QUERY_BITS_CAMERA_BLOCKING, &wcResults[3*i+j] );
		}
	}

	if( bHitWorld ) // allow a partially obstructed player
	{

		// Check the various rays that were shot from the cam target to cam position
		for(i = 0; i < 9; i++)
		{
			
			// If the current ray is reported to have collided against something...
			if(wcResults[i].hitSomething)
			{
				float tempDist;

				if (worldCollisionEntryIsTerrainFromWCO(wcResults[i].wco))
				{
					bHitTerrain = true;
				}

				// Find a distance to place the camera to avoid the collision...
				tempDist = wcResults[i].distance;

				if(tempDist < 1.5)
					tempDist = 1.5;

				if(tempDist < targetDist)
					targetDist = tempDist;
			}
		}
	}

	if (dist)
	{
		Vec3 camToEntDir;
		copyVec3(camToEntVec, camToEntDir);
		scaleVec3(camToEntDir, 1/dist, camToEntDir);
		scaleAddVec3( camToEntDir, targetDist - dist, xCameraMatrix[3], xCameraMatrix[3] );
		assert(FINITEVEC3(xCameraMatrix[3]));
	}
}

static S32 martinCamEnabled;
AUTO_CMD_INT(martinCamEnabled, martinCam) ACMD_ACCESSLEVEL(1);
static F32 martinCamSimRate = 200.f;
AUTO_CMD_FLOAT(martinCamSimRate, martinCamSimRate) ACMD_ACCESSLEVEL(1);
static F32 martinCamReduceScale = 0.1f;
AUTO_CMD_FLOAT(martinCamReduceScale, martinCamReduceScale) ACMD_ACCESSLEVEL(1);
static F32 martinCamSnapAngle = RAD(10.f);
AUTO_CMD_FLOAT(martinCamSnapAngle, martinCamSnapAngle) ACMD_ACCESSLEVEL(1);

// This interpolates position and rotation, not distance
static void gclCamera_Interp(GfxCameraController *pCamera, F32 fElapsedTime)
{
	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );

	if(martinCamEnabled){
		GfxMartinCam*	m = &pCamera->martinCam;
		Quat			rotTarget;
		Quat			rotSource;
		Quat			rotCur;

		MINMAX1(martinCamSimRate, 1.f, 10000.f);
		MINMAX1(martinCamReduceScale, 0.0001f, 1.f);

		if(FALSE_THEN_SET(m->interpIsSet)){
			copyVec3(pCamera->campyr, m->interpTargetPYR);
			copyVec3(pCamera->campyr, m->interpSourcePYR);
		}

		m->accSecondsInterp += fElapsedTime;

		FOR_BEGIN(i, 2);
		{
			F32 diff = subAngle(pCamera->targetpyr[i], pCamera->campyr[i]);
			S32 snap = 1;

			if(diff > 1.1f * martinCamSnapAngle){
				diff -= martinCamSnapAngle;
			}
			else if(diff < 1.1f * -martinCamSnapAngle){
				diff += martinCamSnapAngle;
			}else{
				snap = 0;
			}

			if(snap){
				pCamera->campyr[i] = addAngle(pCamera->campyr[i], diff);
				m->accSecondsInterp = 0.f;
				copyVec3(pCamera->campyr, m->interpSourcePYR);
				PYRToQuat(pCamera->campyr, rotSource);
				PYRToQuat(pCamera->targetpyr, rotTarget);
				quatInterp(martinCamReduceScale, rotSource, rotTarget, rotCur);
				quatToPYR(rotCur, m->interpTargetPYR);
			}
		}
		FOR_END;

		while(m->accSecondsInterp >= 1.f / martinCamSimRate){
			m->accSecondsInterp -= 1.f / martinCamSimRate;

			copyVec3(m->interpTargetPYR, pCamera->campyr);

			copyVec3(pCamera->campyr, m->interpSourcePYR);

			PYRToQuat(pCamera->campyr, rotSource);
			PYRToQuat(pCamera->targetpyr, rotTarget);
			quatInterp(martinCamReduceScale, rotSource, rotTarget, rotCur);
			quatToPYR(rotCur, m->interpTargetPYR);
		}

		PYRToQuat(m->interpSourcePYR, rotSource);
		PYRToQuat(m->interpTargetPYR, rotTarget);
		quatInterp(martinCamSimRate * m->accSecondsInterp, rotSource, rotTarget, rotCur);
		quatToPYR(rotCur, pCamera->campyr);
	}else{
		S32 i;

		pCamera->martinCam.interpIsSet = 0;

		for(i = 0; i < 3; i++)
		{
			F32 fTurningNorm, fDelta, fAngleDiff;

			pCamera->targetpyr[i] = fixAngle(pCamera->targetpyr[i]);

			if (!FINITE(pCamera->targetpyr[i]))
			{
				devassertmsg(0,"Camera PYR is undefined");
				setVec3(pCamera->targetpyr,0,0,0);
				setVec3(pCamera->campyr,0,0,0);
			}

			fAngleDiff = subAngle(pCamera->targetpyr[i], pCamera->campyr[i]);

			fTurningNorm = fAngleDiff / MAXF(pSettings->fRotInterpBasis, 0.01f);
			fTurningNorm = ABS(fTurningNorm);
			fTurningNorm = CLAMP(fTurningNorm, pSettings->fRotInterpNormMin, pSettings->fRotInterpNormMax);
			fDelta = fTurningNorm * pSettings->pyrInterpSpeed[i] * fElapsedTime;

			if (fDelta > ABS(fAngleDiff))
			{
				fDelta = fAngleDiff;
			}
			else if (fAngleDiff < 0.0f)
			{
				fDelta = -fDelta;
			}

			pCamera->campyr[i] = addAngle(pCamera->campyr[i], fDelta);
		}
	}
	
	// testing no interp
#if 0
	copyVec3(pCamera->targetpyr, pCamera->campyr);
#endif
}

// adjusting the camera pitch based on terrain
static F32 gclCamera_CalcUphillPitchAdjust(Entity *pEnt)
{
	Vec3 entPos, entVelocity;
	//U32 color = 0xffff0000;
	F32 uphillPitchAdjust, firstUphillHeight = 0.0f, secondUphillHeight = 0.0f;
	Vec3 src, dst;
	WorldCollCollideResults wcResult;

	entGetPos(pEnt, entPos);
	entCopyVelocityFG(pEnt, entVelocity);
	entVelocity[1] = 0.0f; // we only care about x/z velocity
	copyVec3(entPos, src);
	copyVec3(src, dst);
	src[1] = 999999.0f;
	dst[1] = -999999.0f;
	// if we arent standing over terrain, or we are above or below it by too great a degree, dont adjust camera
	if (!worldCollideRay(PARTITION_CLIENT, src, dst, WC_FILTER_BIT_TERRAIN, &wcResult)
		|| abs(wcResult.posWorldImpact[1]-entPos[1]) > 1.0f)
	{
		return 0.0f;
	}
	//gfxDrawLine3D(src, dst, ARGBToColor(color));

	addVec3(src, entVelocity, src);
	dst[0] = src[0];
	dst[2] = src[2];
	// find terrain height at the position (player_pos + player_velocity)
	if (worldCollideRay(PARTITION_CLIENT, src, dst, WC_FILTER_BIT_TERRAIN, &wcResult))
	{
		//color = 0xff00ff00;
		firstUphillHeight = wcResult.posWorldImpact[1];
	}

	addVec3(src, entVelocity, src);
	dst[0] = src[0];
	dst[2] = src[2];
	//color = 0xffff0000;
	// find terrain height at the position (player_pos + player_velocity * 2)
	if (worldCollideRay(PARTITION_CLIENT, src, dst, WC_FILTER_BIT_TERRAIN, &wcResult))
	{
		//color = 0xff00ff00;
		secondUphillHeight = wcResult.posWorldImpact[1];
	}
	//gfxDrawLine3D(src, dst, ARGBToColor(color));

	{
		F32 deltaX = lengthVec3(entVelocity);
		F32 slope;
		if (deltaX <= 0.0f)
			slope = 0;
		else
			slope = (secondUphillHeight-firstUphillHeight)/deltaX;
		uphillPitchAdjust = slope;
	}

	if (uphillPitchAdjust >= 0)
		uphillPitchAdjust = MIN(uphillPitchAdjust, 0.8f);
	else
		uphillPitchAdjust = MAX(uphillPitchAdjust, -0.8f);

	return uphillPitchAdjust * (HALFPI);
}

static void gclCamLockToTarget_0(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime)
{
	Entity* pEnt = gclCamera_GetEntity();
	Mat3 target_mat;
	Vec3 camToTargetDir;
	Vec3 tempPyr;
	Vec3 resultCenter;
	Vec3 camCenter;
	Vec3 lockedCenter;
	Vec3 resultCenterOffset;
	Vec3 resultCenterRightVec;
	Vec3 playerDir;
	Vec3 upVector = {0, 1, 0};
	F32 rightVecScale = 0.0f;
	F32 playerVel, playerDirDot;
	CameraSettings* pSettings = gclCamera_GetSettings( pCamera );
	TargetLockCameraSettings* pModeSettings = (TargetLockCameraSettings*)pSettings->pModeSettings;

	entCopyVelocityFG(pEnt, playerDir);
	playerVel = lengthVec3(playerDir);
	entGetPosDir(pEnt, NULL, playerDir);
	normalVec3(playerDir);

	// find the position halfway between the player and target
	copyVec3(pCamera->camcenter, camCenter);
	camCenter[1] += gclCamera_GetPlayerHeight(pCamera);
	copyVec3(pCamera->lockedcenter, lockedCenter);
	lockedCenter[1] += gclCamera_GetPlayerHeight(pCamera);
	subVec3(lockedCenter, camCenter, resultCenter);
	scaleVec3(resultCenter, 0.5f, resultCenter);
	addVec3(camCenter, resultCenter, resultCenter);

	// offset the result
	subVec3(resultCenter, camCenter, resultCenterOffset);
	crossVec3(upVector, resultCenterOffset, resultCenterRightVec);
	normalVec3(resultCenterRightVec);

	// figure out how much to offset
	playerDirDot = dotVec3(playerDir, resultCenterRightVec);
	if (!pSettings->bInitialTargetLockHandled)
	{
		Vec3 camLookAtVec;
		copyVec3(pCamera->last_camera_matrix[2], camLookAtVec);
		scaleVec3(camLookAtVec, -1.0f, camLookAtVec);
		playerDirDot = dotVec3(camLookAtVec, resultCenterRightVec);
		if (playerDirDot < 0.0f)
		{
			pModeSettings->fOffset = -pModeSettings->fMaxOffset;
		}
		else
		{
			pModeSettings->fOffset = pModeSettings->fMaxOffset;
		}
		pSettings->bInitialTargetLockHandled = true;
	}
	else if (playerDirDot < 0.0f)
	{
		pModeSettings->fOffset -= playerVel * pModeSettings->fAdjustSpeed * fElapsedTime;
	}
	else
	{
		pModeSettings->fOffset += playerVel * pModeSettings->fAdjustSpeed * fElapsedTime;
	}
	pModeSettings->fOffset = MIN(MAX(pModeSettings->fOffset, -pModeSettings->fMaxOffset), pModeSettings->fMaxOffset);
	rightVecScale = pModeSettings->fOffset;
	
	scaleVec3(resultCenterRightVec, rightVecScale, resultCenterOffset);
	addVec3(resultCenter, resultCenterOffset, resultCenter);

	// find the pyr of the result
	subVec3(camCenter, resultCenter, camToTargetDir );
	normalVec3(camToTargetDir);
	orientMat3(target_mat, camToTargetDir);
	getMat3YPR(target_mat, tempPyr);

	pCamera->targetpyr[0] = tempPyr[0];
	pCamera->targetpyr[1] = tempPyr[1];
	devassert(FINITE(pCamera->targetpyr[1]));

}

__forceinline static void gclCamera_ConvertEntityPYToCamPy(const Vec2 pyFace, Vec2 camPYFace)
{
	camPYFace[0] = pyFace[0] * -1.f;
	camPYFace[1] = addAngle(pyFace[1], PI);
}

void gclUpdateControlsInputMoveFace(Entity *pEnt, F32 yaw);

// ------------------------------------------------------------------------------------------------------------------
void gclCamera_TurnToFaceTarget(void)
{
	Vec3 vSelectedTargetPos, eMePos;
	Entity* pEnt;

	pEnt = gclCamera_GetEntity();
	if (pEnt && pEnt->pChar && entGetClientSelectedTargetPos(pEnt, vSelectedTargetPos)) 
	{
		GfxCameraController *pCamera = gfxGetActiveCameraController();
		CameraSettings *pSettings = pCamera ? gclCamera_GetSettings(pCamera) : NULL;
		
		ANALYSIS_ASSUME(pEnt != NULL);
		if(pSettings && !pSettings->bIsMouseLooking)
		{
			Vec3 d;

			entGetPos(pEnt, eMePos);
			subVec3(vSelectedTargetPos, eMePos, d);
			pCamera->targetpyr[1] = addAngle(getVec3Yaw(d), PI);
			devassertmsg(FINITEVEC3(pCamera->targetpyr), "Undefined pCamera PYR!");

			gclUpdateControlsInputMoveFace(pEnt, addAngle(pCamera->targetpyr[1], PI));
		}
	}
}


// ------------------------------------------------------------------------------------------------------------------
bool gclCamera_IsMouseLooking(GfxCameraController *pCamera)
{
	if (pCamera)
	{
		CameraSettings *pSettings = gclCamera_GetSettings( pCamera );
		return (pSettings) ? pSettings->bIsMouseLooking  : false;
	}
	
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
bool gclCamera_ShouldTurnToFace(void)
{
	GfxCameraController *pCamera = gfxGetActiveCameraController();
	if (pCamera)
	{
		CameraMode eMode = gclCamera_GetSettings(pCamera)->eMode;

		switch (eMode)
		{
			case kCameraMode_TurnToFace:
			case kCameraMode_ShooterCamera:
			{
				return true;
			}
		}
	}
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
void gclCamera_MatchPlayerFacing(bool matchPitch, bool bForce, bool useOverridePitch, F32 fOverridePitch)
{
	Vec2 pyFace;
	GfxCameraController *pCamera;
	CameraSettings *pSettings;
	
	if (!bForce && (g_CurrentScheme.eCameraFollowType == kCameraFollowType_Never || g_CurrentScheme.eCameraFollowType == kCameraFollowType_NoSnap))
	{
		return;
	}

	pCamera = gfxGetActiveCameraController();
	pSettings = (pCamera) ? gclCamera_GetSettings(pCamera) : NULL;
	if (!pSettings || pSettings->bIsMouseLooking)
		return;
	
	if (gclPlayerControl_GetCameraSnapToFacing(pyFace))
	{	
		gclCamera_ConvertEntityPYToCamPy(pyFace, pyFace);
		pCamera->targetpyr[1] = pyFace[1];
		if (matchPitch)
		{
			if (useOverridePitch)
				pyFace[0] = fOverridePitch;
			else
				pCamera->targetpyr[0] = pyFace[0];
		}

		devassertmsg(FINITEVEC3(pCamera->targetpyr), "Undefined pCamera PYR!");
		
		{
			CameraInterpSpeed* pInterpSpeed = eaIndexedGetUsingInt(&pSettings->eaRotInterpSpeeds, ECameraInterpSpeed_SLOW);
			if (pInterpSpeed)
			{
				gclCamera_SetPYInterpSpeed(pSettings, pInterpSpeed->fSpeed);
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void gclCamera_AutoAdjust(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime)
{
	F32 xdiff = 0, ydiff = 0, entSpeed = 0.0f;
	Entity* pEnt = gclCamera_GetEntity();
	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );
	bool bShouldFollow = pEnt && entGetWorldRegionTypeOfEnt(pEnt) != WRT_Space && g_CurrentScheme.eCameraType == kCameraType_Follow;
	bool bAutoAdjust = bShouldFollow || pCamera->lockedtotarget;
	
	gamepadGetLeftStick(&xdiff, &ydiff);

	if (fabs(xdiff) < 0.3 && fabs(ydiff) < 0.3) // dead zone hack for now
	{
		xdiff = 0;
		ydiff = 0;
		if (!isPlayerMoving())
		{
			bAutoAdjust = false;
		}
	}
	else
	{
		gclCamera_ForceAutoAdjust(false);
	}
	
	if (!pEnt)
		return;

	if (pSettings->bCameraFirstFrameAfterDefault)
	{
		bAutoAdjust = false;
	}

	if(bAutoAdjust || pSettings->bForceAutoAdjust && !(pCamera->rotate_left || pCamera->rotate_right))
	{	
		Vec3 vSelectedTargetPos;

		// the check here for g_CombatConfig should probably be in some other camera control config, 
		// but this is all i have for now
		if (g_CombatConfig.bMoveDuringPowerActivation && pEnt && pEnt->pChar && gclPlayerControl_IsFacingTarget()) 
		{
			// Sam's crappy temporary camera for run and gun
			Vec3 vToTarget;
			F32 fTargetYaw, fDiffYaw;

			if (!entGetClientSelectedTargetPos(pEnt, vSelectedTargetPos))
				return;

			subVec3(vSelectedTargetPos, pCamera->camcenter, vToTarget);

			fTargetYaw = getVec3Yaw(vToTarget) + PI;
			fDiffYaw = fTargetYaw - pCamera->targetpyr[1];

			if (fDiffYaw > PI)
				fDiffYaw -= TWOPI;
			else if (fDiffYaw < -PI)
				fDiffYaw += TWOPI;

			if (fabs(fDiffYaw) > HALFPI)
				// If it's too far away don't try to catch up
				fDiffYaw = 0;
				

			fDiffYaw *= CLAMP(fElapsedTime, 0.0f, 1.0f);

			if (fabs(fDiffYaw) < 0.001f)
				fDiffYaw = 0.0f;

			pCamera->targetpyr[1] += fDiffYaw;

			if (pCamera->targetpyr[1] > PI)
				pCamera->targetpyr[1] -= TWOPI;
			else if (pCamera->targetpyr[1] < -PI)
				pCamera->targetpyr[1] += TWOPI;

			// as we're using this targeting, lock out the auto-adjust for a time so when we 
			// suddenly lose a target the auto-adjust doesn't come back full force
			pCamera->autoAdjustTimeout = s_fAutoAdjustTimeout * 0.5f;
			devassert(FINITE(pCamera->targetpyr[1]));
		}
		else if (!pCamera->lockAutoAdjust && (bShouldFollow) && (xdiff || ydiff))
		{
			Vec3 vToTargetCur;
			Vec3 vEntVelocity;
			F32 fAngle, fVelocity, fDistToEnt;

			gclCamera_ResetPYRInterpToController(pSettings);
			
			if (pCamera->autoAdjustTimeout > 0)
				return;

			entCopyVelocityFG(pEnt, vEntVelocity);
			vEntVelocity[1] = 0;
			fVelocity = normalVec3(vEntVelocity);
			// not moving, do nothing
			if (fVelocity == 0.f)
			{	
				pCamera->targetpyr[1] = pCamera->campyr[1];
				devassertmsg(FINITE(pCamera->targetpyr[1]), "Undefined pCamera PYR!");
				pSettings->bIsCameraRotatingBoom = false;
				return;
			}
		
			subVec3(pCamera->camcenter, pCamera->last_camera_matrix[3], vToTargetCur);
			vToTargetCur[1] = 0.f;
			fDistToEnt = normalVec3(vToTargetCur);
			
			
		#define TOWARD_ANGLE_THRESHOLD		RAD(135.f)
		#define AWAYFROM_ANGLE_THRESHOLD	RAD(30.f)
			fAngle = dotVec3(vToTargetCur, vEntVelocity);
			fAngle = acosf( CLAMP(fAngle, -1.f, 1.f));
			if (fAngle > TOWARD_ANGLE_THRESHOLD)
			{	// heading towards the camera, stop any camera turning
				pCamera->targetpyr[1] = pCamera->campyr[1];
				devassertmsg(FINITE(pCamera->targetpyr[1]), "Undefined pCamera PYR!");
				pSettings->bIsCameraRotatingBoom = false;
				return;
			}
			if (fAngle < AWAYFROM_ANGLE_THRESHOLD)
			{
				// heading directly away from the camera
				// only stop any camera turning if we are going very close to directly away
				// this prevents a border camera jitter problem
				if (fAngle < RAD(25.f))
				{
					pCamera->targetpyr[1] = pCamera->campyr[1];
					devassertmsg(FINITE(pCamera->targetpyr[1]), "Undefined pCamera PYR!");
					pSettings->bIsCameraRotatingBoom = false;
				}

				return;
			}
			
			if (fVelocity > 10.f)
			{
				negateVec3(vEntVelocity, vEntVelocity);
				pCamera->targetpyr[1] = getVec3Yaw(vEntVelocity);
				devassertmsg(FINITE(pCamera->targetpyr[1]), "Undefined pCamera PYR!");
				pSettings->bIsCameraRotatingBoom = true;
			}
	
		}
	}
	else if (pSettings->bIsCameraRotatingBoom)
	{
		pSettings->bIsCameraRotatingBoom = false;
		pCamera->targetpyr[1] = pCamera->campyr[1];
		devassertmsg(FINITE(pCamera->targetpyr[1]), "Undefined pCamera PYR!");
	}
}

// ------------------------------------------------------------------------------------------------------------------
// does a weighted average over time.  Used by gclCamera_GetSmoothDelta2 for Matt's old input smoothing code,
// and also for my newer "focus smoothing". [RMARR]
static void gclCamera_GetSmoothVal(CameraSmoothNode*** peaNodes, 
									 F32 fSampleMultiplier,
									 F32 fSampleTime,
									 F32 fElapsedTime,
									 const Vec3 vInput, 
									 Vec3 vOutput)
{
	S32 i, iHistorySize = eaSize(peaNodes);
	CameraSmoothNode* pNode = NULL;
	CameraSmoothNode* pLastNode = eaGet(peaNodes,0);
	F32 fHistoryElapsed = pLastNode ? (gGCLState.totalElapsedTimeMs-pLastNode->uTime)/1000.0f : 0.0f;
	F32 fLastWeight = 1.0f;
	F32 fInterp = 0.0f;
	F32 fInvSampleMultiplier = 1/MAXF(fSampleMultiplier,0.01f);
	Vec3 vBase;
	vBase[0] = vInput[0]-fmodf(vInput[0],10.0f);
	vBase[1] = vInput[1]-fmodf(vInput[1],10.0f);
	vBase[2] = vInput[2]-fmodf(vInput[2],10.0f);

	setVec3(vOutput, 0.0f, 0.0f, 0.0f);
	if (pLastNode && fHistoryElapsed-pLastNode->fElapsedTime >= fSampleTime)
	{
		pNode = eaRemove(peaNodes, 0);
	}
	else
	{
		pNode = StructCreate(parse_CameraSmoothNode);
	}
	if (pNode)
	{
		copyVec3(vInput, pNode->vVal);
		pNode->fElapsedTime = fElapsedTime;
		pNode->uTime = gGCLState.totalElapsedTimeMs-fElapsedTime*1000;
		eaPush(peaNodes, pNode);
	}
	for (i = eaSize(peaNodes)-1; i >= 0; i--)
	{
		F32 fWeight;
		Vec3 vVal;
		pNode = (*peaNodes)[i];
		fInterp += pNode->fElapsedTime/fSampleTime;
		MIN1F(fInterp,1.0f);
		if (fInvSampleMultiplier > 1.0f)
		{
			fWeight = fLastWeight - 1/powf(fInvSampleMultiplier, fInterp);
		}
		else
		{
			fWeight = fInterp;
		}
		subVec3(pNode->vVal,vBase,vVal);
		scaleAddVec3(vVal, fWeight, vOutput, vOutput);
		fLastWeight -= fWeight;
	}
	scaleByVec3(vOutput, 1/MAXF(1-fLastWeight,0.001f));
	addVec3(vOutput,vBase,vOutput);
}

// ------------------------------------------------------------------------------------------------------------------
// operates on Vec2's, but uses the Vec3 generic version to do it, at this time.
// Used for Matt's old input smoothing code
static void gclCamera_GetSmoothDelta2(CameraSmoothNode*** peaNodes, 
									 F32 fSampleMultiplier,
									 F32 fSampleTime,
									 F32 fElapsedTime,
									 const Vec2 vInputDelta, 
									 Vec2 vOutputDelta)
{
	Vec3 vTempInput,vTempOutput;
	vTempInput[0] = vInputDelta[0];
	vTempInput[1] = vInputDelta[1];
	vTempInput[2] = 0.0f;
	gclCamera_GetSmoothVal(peaNodes,fSampleMultiplier,fSampleTime,fElapsedTime,vTempInput,vTempOutput);
	vOutputDelta[0] = vTempOutput[0];
	vOutputDelta[1] = vTempOutput[1];
}

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
// These structs are related to my experimental smoothing algo, described below [RMARR - 11/1/12]
typedef struct MouseValue
{
	Vec2 vDelta; // delta that was observed
	// delta divided by time - this is here in case we need to do more precise checks to stop the smoothing
	//Vec2 vNormalizedDelta; 
	// Times are all in ms right now.  Doesn't really matter
	F32 fTime; // time over which that delta was observed
	F32 fTimeRemaining; // time that hasn't been used yet
} MouseValue;

// mostly a simple ring buffer
#define MOUSE_BUFFER_VALUES 4
typedef struct MouseBuffer
{
	MouseValue aValues[MOUSE_BUFFER_VALUES];
	int iFirstValBufferPosition; // the least recent value
	// sometimes, slots will just be used to store framerates.  This variable says how much actual data we have left to distribute.
	// Under stable conditions, this will be just the current frame.
	F32 fCurrentStoredTime;
} MouseBuffer;

static MouseBuffer g_buffer = {0};

void mouseBufferAddSample(MouseBuffer * pBuffer,Vec2 const vVal,F32 fTime)
{
	int iSlot = pBuffer->iFirstValBufferPosition;

	pBuffer->iFirstValBufferPosition = (pBuffer->iFirstValBufferPosition+1)%MOUSE_BUFFER_VALUES;

	assert(pBuffer->aValues[iSlot].fTimeRemaining == 0.0f);

	pBuffer->aValues[iSlot].fTime = pBuffer->aValues[iSlot].fTimeRemaining = fTime;
	copyVec2(vVal,pBuffer->aValues[iSlot].vDelta);
//	scaleVec2(vVal,1.0f/fTime,pBuffer->aValues[iSlot].vNormalizedDelta);

	pBuffer->fCurrentStoredTime += fTime;
}

F32 mouseBufferGetCurrentAverageFrameTime(MouseBuffer * pBuffer)
{
	int i;
	F32 fTotal=0.0f;
	for (i=0;i<MOUSE_BUFFER_VALUES;i++)
	{
		fTotal += pBuffer->aValues[i].fTime;
	}
	return fTotal/MOUSE_BUFFER_VALUES;
}

void mouseBufferGetDeltaForCurrentFrameTime(MouseBuffer * pBuffer,Vec2 vOut)
{
	F32 fAverageFrameTime = mouseBufferGetCurrentAverageFrameTime(pBuffer);
	F32 fStoredTime = pBuffer->fCurrentStoredTime;
	F32 fAmountToGrab;
	int i;

	fAmountToGrab = MIN(fStoredTime*0.5f+fAverageFrameTime*0.5f,fStoredTime);

	zeroVec2(vOut);

	for (i=0;i<MOUSE_BUFFER_VALUES;i++)
	{
		int iSlot = (i+pBuffer->iFirstValBufferPosition)%MOUSE_BUFFER_VALUES;

		if (pBuffer->aValues[iSlot].fTimeRemaining)
		{
			F32 fAmountOfSlotToGrab;
			fAmountOfSlotToGrab = MIN(fAmountToGrab,pBuffer->aValues[iSlot].fTimeRemaining);

			scaleAddVec2(pBuffer->aValues[iSlot].vDelta,fAmountOfSlotToGrab/pBuffer->aValues[iSlot].fTime,vOut,vOut);

			pBuffer->aValues[iSlot].fTimeRemaining -= fAmountOfSlotToGrab;
		}
	}

	pBuffer->fCurrentStoredTime -= fAmountToGrab;
}

// A shot at a more sophisticated input smoothing algo, that is less likely to induce vomiting [RMARR - 10/29/12]
static void gclCamera_DoInputSmoothing(MouseBuffer * pBuffer, 
									 const Vec2 vInput, 
									 Vec2 vOutput)
{
	static Vec2 vLastDelta = {0};
	Vec2 vTemp;
	F32 fTime;

	static F32 NO_SMOOTH_THRESHOLD_SQ = 0.02f;
	static F32 REVERSE_THRESHOLD_DOT = 1e-6f;

	fTime = mouseGetTimesliceMS();

	// An explanation of what is going on here:
	// The idea is primarily to address the scenario where we get divergent "sample times" of mouse input.  This is totally normal and happens all the time.
	// So, for example, you might get 3 frames where the sample window was about ~16ms, and suddenly one where it's about ~33ms.  We don't want to
	// respond to this by suddenly applying a double movement step to the camera.  The reason  for this is, the user has already spent longer looking at a frame
	// than you intended, and suddenly jerking the camera ahead to compensate for that is just going to accentuate that fact, and make things feel "jittery".
	// However, we DO have to adapt to variable frame lengths.  This ring buffer is designed to amortize the effect of varying time steps.
	mouseBufferAddSample(pBuffer,vInput,fTime);

	mouseBufferGetDeltaForCurrentFrameTime(pBuffer,vOutput);

	// This last step is just a simple ONE FRAME averaging, to account for the quantized nature of mouse input.  However, we never want to give the player
	// the feeling that the mouse is unresponsive, so we bail out if the mouse delta is large, or in the opposite direction of the previous move, so
	// we check for those cases and skip the averaging.
	copyVec2(vLastDelta,vTemp);
	copyVec2(vOutput,vLastDelta);

	if (lengthVec2Squared(vInput) > NO_SMOOTH_THRESHOLD_SQ)
		return;

	if (dotVec2(vInput,vTemp) < REVERSE_THRESHOLD_DOT)
		return;

	lerpVec2(vTemp,0.4f,vOutput,vOutput);
}

static int iCamTest=0;
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(cam_test_rob);
void gclCamera_SetCamTest(int iTest)
{
	iCamTest = iTest;
}
// ------------------------------------------------------------------------------------------------------------------
// returns true if there was any mouse input detected
static bool gclCamera_GetMouseInput(GfxCameraController *pCamera, CameraSettings *pSettings, F32 fElapsedTime, Vec3 vCamMovePYOut)
{
	Vec2 vCamMovePY, vMouseDelta;
	SmoothCameraSettings *pSmoothSettings;
	F32 fMouseFilter;

	pSmoothSettings = &s_DefaultCameraModeSettings.SmoothSettings;
		
	fMouseFilter = pSettings->bSmoothCamera ? pSmoothSettings->fMouseFilter : pSettings->fMouseFilter;
	fMouseFilter = CLAMPF32(fMouseFilter, 0.0f, 1.0f);
	mouseDiffNormalized(&vMouseDelta[0], &vMouseDelta[1]);

	
	if (g_bLockYaw) 
	{
		vMouseDelta[0] = 0.0f;
	} 
	else 
	{
		vMouseDelta[0] *= input_state.invertX? -1.f : 1.f;
	}

	if (g_bLockPitch) 
	{
		vMouseDelta[1] = 0.0f;
	} 
	else 
	{
		vMouseDelta[1] *= input_state.invertY? -1.f : 1.f;
	}

	if (gConf.bSmartInputSmoothing)
	{
		Vec2 vSmoothDelta;
		gclCamera_DoInputSmoothing(	&g_buffer,
							vMouseDelta,
							vSmoothDelta);

		copyVec2(vSmoothDelta,vMouseDelta);
	}
	else if (fMouseFilter > FLT_EPSILON)
	{
		Vec2 vSmoothDelta;
		F32 fSampleTime = pSmoothSettings->fFilterSampleTime;
		gclCamera_GetSmoothDelta2(	&pSmoothSettings->eaSmoothHistory,
									fMouseFilter,
									fSampleTime,
									fElapsedTime,
									vMouseDelta,
									vSmoothDelta);

		copyVec2(vSmoothDelta,vMouseDelta);
		if (pSettings->bSmoothCamera && pSmoothSettings->bUseSplineCurve)
		{
			//TODO(MK): if further smoothing is necessary, construct a b-spline curve from the history array
			devassert(0);
		}
	}
	else
	{
		eaClearStruct(&pSmoothSettings->eaSmoothHistory, parse_CameraSmoothNode);
	}

	setVec2(vCamMovePY, vMouseDelta[1]*pSettings->pyrMouseSpeed[0], vMouseDelta[0]*pSettings->pyrMouseSpeed[1]);

	gclCamera_UpdateMouseInputDamping(pSettings, pCamera, fElapsedTime, vCamMovePY);
	
	if(martinCamEnabled)
	{
		pCamera->targetpyr[1] = addAngle(pCamera->targetpyr[1], vCamMovePY[1]);
		devassert(FINITE(pCamera->targetpyr[1]));
		pCamera->targetpyr[0] = addAngle(pCamera->targetpyr[0], vCamMovePY[0]);
		devassert(FINITE(pCamera->targetpyr[0]));
	}
	else if (!pSettings->bSmoothCamera || !pSmoothSettings->bUseSplineCurve)
	{
		pCamera->targetpyr[1] = pCamera->campyr[1] = addAngle(pCamera->campyr[1], vCamMovePY[1]);
		devassert(FINITE(pCamera->targetpyr[1]));
		pCamera->targetpyr[0] = pCamera->campyr[0] = addAngle(pCamera->campyr[0], vCamMovePY[0]);
		devassert(FINITE(pCamera->targetpyr[0]));
	}

	if (vCamMovePYOut)
		copyVec2(vCamMovePY, vCamMovePYOut);

	pSettings->bCameraIsDefault = false;

	if (!gfxGetFullscreen())		
	{
		if (!ClientWindowIsBeingMovedOrResized())
		{
			if (GetForegroundWindow() == gfxGetWindowHandle())
			{
				RECT rect;
				POINT zero = {0, 0};
				GetClientRect(gfxGetWindowHandle(), &rect);
				ClientToScreen(gfxGetWindowHandle(), &zero);
				rect.left += zero.x;
				rect.top += zero.y;
				rect.right += zero.x;
				rect.bottom += zero.y;
				ClipCursor(&rect);

				if (!ui_IsModalShowing())
					mouseLock(1);
			}
			else
			{
				mouseLock(0);
			}
		}
	}
	else
	{
		if (GetForegroundWindow() == gfxGetWindowHandle() && !ui_IsModalShowing())
		{		
			mouseLock(1);
		}
		else
		{
			mouseLock(0);
		}
	}

	gclCamera_ForceAutoAdjust(false);
	
	// rotating the camera manually overrides the match player facing
	gclResetPYRInterpSpeed(pSettings);

	return !vec2IsZero(vCamMovePY);
}


// ------------------------------------------------------------------------------------------------------------------
static void gclCamera_GetInput(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime)
{
	F32 xdiff = 0, ydiff = 0;
	Entity *pEnt;
	Vec2 vCamMovePY = {0};
	bool bRotated = false;
	CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
	SmoothCameraSettings *pSmoothSettings = &s_DefaultCameraModeSettings.SmoothSettings;

	if (fElapsedTime > 1) // if we are getting lest than 1fps, assume its a stall and stop spinning camera
		return;

	if (!pCamera->inited)
	{
		if (!pCamera->pyr_preinit)
		{
			setVec3(pCamera->campyr, 0,0,0);
			setVec3(pCamera->targetpyr, 0,0,0);
		}
		pCamera->targetdist = pCamera->camdist = pSettings->pfDistancePresets[1];
		pCamera->inited = 1;
		pCamera->pyr_preinit = 0;
	}
	
	// Grab the yaw from the player
	pEnt = gclCamera_GetEntity();
	if (pEnt)
	{
		Quat rot;
		Vec3 pyrEnt;
		entGetRot(pEnt, rot);
		quatToPYR(rot,pyrEnt);

		if (fabs(pSettings->fLastPlayerYaw - pyrEnt[1]) > RAD(15))
		{
			pSettings->bCameraIsDefault = false;
		}
	}

	// Player is moving camera with joypad
	if (!g_bTemporarilyDisableCameraControls)
	{
		gamepadGetRightStick(&xdiff, &ydiff);
	}
	
	// TESTING - Spinning the camera is very irritating with a small pitch change spins you all around-------------
	if(ABS(xdiff)<ABS(ydiff)/4)
		xdiff = 0;
	if(ABS(xdiff)/4>ABS(ydiff))
		ydiff = 0;

	xdiff *= input_state.invertX?-1:1;
	ydiff *= input_state.invertY?-1:1;

	if((xdiff || ydiff) && !(pCamera->lockControllerCamControl || gGCLState.bLockControllerCameraControl || gGCLState.bLockPlayerAndCamera))
	{
		pSettings->bCameraIsDefault = false;

		// we are forcing the pyr interpolation here by snapping the targetPYR
		pCamera->campyr[1] = addAngle(pCamera->campyr[1], fElapsedTime * xdiff * pSettings->pyrJoypadSpeed[1]);
		pCamera->targetpyr[1] = pCamera->campyr[1];
		devassert(FINITE(pCamera->targetpyr[1]));
		pCamera->campyr[0] = addAngle(pCamera->campyr[0], fElapsedTime * -ydiff * pSettings->pyrJoypadSpeed[0]);		
		pCamera->targetpyr[0] = pCamera->campyr[0];

		pCamera->autoAdjustTimeout = s_fAutoAdjustTimeout;
		
		gclCamera_ResetPYRInterpToController(pSettings);
		bRotated = true;
	}
	else if (pCamera->autoAdjustTimeout > 0)
	{
		pCamera->autoAdjustTimeout -= fElapsedTime;
	}

	// Player is moving camera with keyboard
	if ( !pSettings->bIsMouseLooking && (pCamera->rotate_left ^ pCamera->rotate_right) )
	{
		pSettings->bCameraIsDefault = false;

		xdiff = 0;
		if (pCamera->rotate_left)
			xdiff -= 1;
		if (pCamera->rotate_right)
			xdiff +=1;

		// we are forcing the pyr interpolation here by snapping the targetPYR
		pCamera->campyr[1] = addAngle(pCamera->campyr[1], fElapsedTime * xdiff * pSettings->pyrKeyboardSpeed[1]);
		pCamera->targetpyr[1] = pCamera->campyr[1];
		devassert(FINITE(pCamera->targetpyr[1]));

		{
			CameraInterpSpeed* pInterpSpeed = 
				eaIndexedGetUsingInt(&pSettings->eaRotInterpSpeeds, ECameraInterpSpeed_FAST1);
			if (pInterpSpeed)
			{
				gclCamera_SetPYInterpSpeed(pSettings, pInterpSpeed->fSpeed);
			}
		}
		bRotated = true;
	}
	
	if (!pSettings->bIsMouseLooking && (pCamera->rotate_up ^ pCamera->rotate_down))
	{
		ydiff = 0;
		if (pCamera->rotate_down)
			ydiff += 1;
		if (pCamera->rotate_up)
			ydiff -= 1;

		// we are forcing the pyr interpolation here by snapping the targetPYR
		pCamera->campyr[0] = addAngle(pCamera->campyr[0], fElapsedTime * ydiff * pSettings->pyrKeyboardSpeed[0]);
		pCamera->targetpyr[0] = pCamera->campyr[0];
		devassert(FINITE(pCamera->targetpyr[0]));

		bRotated = true;
	}

	
	if (pSettings->bIsMouseLooking && !gGCLState.bLockPlayerAndCamera)
	{
		gclCamera_GetMouseInput(pCamera, pSettings, fElapsedTime, vCamMovePY);

		bRotated = true;
		gclResetPYRInterpSpeed(pSettings);
	}
	else
	{
		eaClearStruct(&pSmoothSettings->eaSmoothHistory, parse_CameraSmoothNode);
		
		if (!gfxGetFullscreen())
		{
			ClipCursor(NULL);
		}
		mouseLock(0);
	}

	gclCamera_UpdateCameraLookAtOverride(pCamera, pSettings, vCamMovePY, bRotated, fElapsedTime);

	// extreme bounds
	if (pCamera->targetpyr[0] > g_fMaxPitch)
		pCamera->targetpyr[0] = g_fMaxPitch;
	if (pCamera->targetpyr[0] < g_fMinPitch)
		pCamera->targetpyr[0] = g_fMinPitch;
	if (pCamera->campyr[0] > g_fMaxPitch)
		pCamera->campyr[0] = g_fMaxPitch;
	if (pCamera->campyr[0] < g_fMinPitch)
		pCamera->campyr[0] = g_fMinPitch;

	if (bRotated)
	{
		gclCamera_ForceAutoAdjust(false);
	}

	// If no camera movement, but player is moving with analog, do auto adjusting
	if ((!bRotated || pSettings->bForceAutoAdjust) && !gGCLState.bLockPlayerAndCamera)
	{
		gclCamera_AutoAdjust(pCamera, pCameraView, fElapsedTime);
	}
	pSettings->bCameraFirstFrameAfterDefault = false;
}

static void rotateAroundPoint(F32 *x, F32 *y, F32 ptx, F32 pty, F32 rot)
{
	Vec3 rotmat;
	Vec3 pos;
	Vec3 pos2;
	Mat3 mat;
	rotmat[0] = rotmat[1] = 0;
	rotmat[2] = rot*PI/180;
	createMat3YPR(mat, rotmat);
	setVec3(pos, *x - ptx, *y - pty, 0);
	mulVecMat3(pos, mat, pos2);
	*x = ptx + pos2[0];
	*y = pty + pos2[1];
}

static void gclCamera_LeashGetInput(GfxCameraController *pCamera, GfxCameraView *pCameraView, Mat4 xCameraMatrix, F32 fElapsedTime)
{
	F32 xdiff = 0, ydiff = 0;
	Vec3 vecToEnt;
	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );

	gamepadGetRightStick(&xdiff, &ydiff);
	xdiff *= input_state.invertX?-1:1;
	ydiff *= input_state.invertY?-1:1;

	subVec3(pCamera->camPos, pCamera->camcenter, vecToEnt);
	normalVec3(vecToEnt);
	if (ydiff)
	{
		pCamera->camPos[0] -= vecToEnt[0] * ydiff * 25.0f * fElapsedTime;
		pCamera->camPos[2] -= vecToEnt[2] * ydiff * 25.0f * fElapsedTime;
	}
	if (xdiff)
	{
		F32 fRot = xdiff*fElapsedTime*50.0f;
		rotateAroundPoint(&pCamera->camPos[0], &pCamera->camPos[2], pCamera->camcenter[0], pCamera->camcenter[2], fRot);
	}

	if (pSettings->bIsMouseLooking)
	{
		F32 mxdiff, mydiff;
		F32 rot = 0.0f;

		mouseDiffNormalized(&mxdiff, &mydiff);
		if (g_bLockYaw)
			mxdiff = 0.0f;
		else
			mxdiff *= input_state.invertX?-1:1;
		if (g_bLockPitch)
			mydiff = 0.0f;
		else
			mydiff *= input_state.invertY?-1:1;

		// adjust the x/z distance to the player based on the camera pitch speed
		pCamera->camPos[0] -= vecToEnt[0] * mydiff * pSettings->pyrMouseSpeed[0] * fElapsedTime;
		pCamera->camPos[2] -= vecToEnt[2] * mydiff * pSettings->pyrMouseSpeed[0] * fElapsedTime;

		// rotating around the y axis based on the camera yaw speed
		rot = (F32)mxdiff*fElapsedTime*pSettings->pyrMouseSpeed[1];
		rotateAroundPoint(&pCamera->camPos[0], &pCamera->camPos[2], pCamera->camcenter[0], pCamera->camcenter[2], rot);

		mouseLock(1);
	}
	else
	{
		mouseLock(0);
	}

	// extreme bounds
	if (pCamera->targetpyr[0] > g_fMaxPitch)
		pCamera->targetpyr[0] = g_fMaxPitch;
	if (pCamera->targetpyr[0] < g_fMinPitch)
		pCamera->targetpyr[0] = g_fMinPitch;
}


void gclCameraLockFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime, F32 fRealElapsedTime)
{
	Entity *pEnt = gclCamera_GetEntity();
	Entity *pTargetEnt;
	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );
	TargetLockCameraSettings *pLockSettings = (TargetLockCameraSettings*)pSettings->pModeSettings;

	pTargetEnt = entFromEntityRefAnyPartition(pEnt->pChar->currentTargetRef);

	if (pTargetEnt && pCamera->lockedtotarget)
	{
		Mat4 cameraMatrix;
		Vec3 playerToTargetVec;
		Vec3 playerPos, targetPos;
		Vec3 playerOffsetPos;
		Vec3 playerDir;
		F32 playerVel, playerDirDot;

		//gclSetCameraTarget(pEnt);

		entCopyVelocityFG(pEnt, playerDir);
		playerVel = lengthVec3(playerDir);
		entGetPosDir(pEnt, NULL, playerDir);
		normalVec3(playerDir);

		entGetPos(pEnt, playerPos);
		entGetPos(pTargetEnt, targetPos);
		playerPos[1] += 6.0f;
		targetPos[1] += 6.0f;
		subVec3(playerPos, targetPos, playerToTargetVec);
		orientMat3(cameraMatrix, playerToTargetVec);

		scaleVec3(cameraMatrix[0], pLockSettings->fOffset, playerOffsetPos);
		addVec3(playerPos, playerOffsetPos, playerOffsetPos);
		subVec3(playerOffsetPos, targetPos, playerToTargetVec);
		orientMat3(cameraMatrix, playerToTargetVec);

		playerDirDot = dotVec3(playerDir, cameraMatrix[0]);
		if (!pSettings->bInitialTargetLockHandled)
		{
			Vec3 camLookAtVec;
			copyVec3(pCamera->last_camera_matrix[2], camLookAtVec);
			scaleVec3(camLookAtVec, -1.0f, camLookAtVec);
			playerDirDot = dotVec3(camLookAtVec, cameraMatrix[0]);
			if (playerDirDot < 0.0f)
				pLockSettings->fOffset = -pLockSettings->fMaxOffset;
			else
				pLockSettings->fOffset = pLockSettings->fMaxOffset;
			pSettings->bInitialTargetLockHandled = true;
		}
		else if (playerDirDot < -0.1f || playerDirDot > 0.1f)
		{
			if (playerDirDot < 0.0f)
				pLockSettings->fOffset += playerVel * pLockSettings->fAdjustSpeed * fElapsedTime;
			else
				pLockSettings->fOffset -= playerVel * pLockSettings->fAdjustSpeed * fElapsedTime;
		}
		pLockSettings->fOffset = MIN(MAX(pLockSettings->fOffset, -pLockSettings->fMaxOffset), pLockSettings->fMaxOffset);

		copyVec3(targetPos, cameraMatrix[3]);
		moveinZ(cameraMatrix, lengthVec3(playerToTargetVec) + 15);
		//cameraMatrix[3][1] += 5.0f;
		moveinZ(cameraMatrix, -gclCamera_GetZDistanceFromTarget(pCamera, cameraMatrix));

		getMat3YPR(cameraMatrix, pCamera->campyr);
		frustumSetCameraMatrix(&pCameraView->new_frustum, cameraMatrix);
	}
	else
	{
		pSettings->bInitialTargetLockHandled = false;
	}
}

static void gclCamera_DoCollisionFade(Entity* pEnt, GfxCameraController *pCamera, Vec3 vCamPos)
{
	CameraSettings *pSettings = gclCamera_GetSettings(pCamera);

	if (pEnt)
	{
		F32 fDistScale = gclCamera_GetDistanceScale(pSettings);
		F32 fPlayerFadeDist;
		F32 fPlayerInvisDist;
		F32 fDistance;

		if (pSettings->bAdjustOffsetsByCapsuleSize)
		{
			const Capsule* pCapsule = entGetPrimaryCapsule(pEnt);
			Vec3 vCamPosExtended, vClose, vEntPos, vCamToEnt;
			F32 fCloseDist, fCapDist;
			F32 fCapsuleExtent = pSettings->fDefaultCapsuleRadius;

			entGetPos(pEnt, vEntPos);
			subVec3(vEntPos, vCamPos, vCamToEnt);
			fDistance = normalVec3(vCamToEnt);

			if (pCapsule)
			{
				fCapsuleExtent = MAXF(pCapsule->fRadius, pCapsule->fLength)+1.0f;
			}
			if (fDistance < fCapsuleExtent)
			{
				scaleAddVec3(vCamToEnt, -fCapsuleExtent, vCamPos, vCamPosExtended);
			}
			else
			{
				copyVec3(vCamPos, vCamPosExtended);
				fCapsuleExtent = 0.0f;
			}

			fCloseDist = entGetDistance(NULL, vCamPosExtended, pEnt, NULL, vClose) - fCapsuleExtent;
			fCapDist = MAXF(fDistance - fCloseDist, 0.0f);

			fPlayerFadeDist = (pSettings->fPlayerFadeDistance + fCapDist) * pSettings->fFadeDistanceScale;
			fPlayerInvisDist = (pSettings->fPlayerInvisibleDistance + fCapDist) * pSettings->fFadeDistanceScale;
		}
		else
		{
			fDistance = pCamera->camdist;
			fPlayerFadeDist = pSettings->fPlayerFadeDistance * fDistScale * pSettings->fFadeDistanceScale;
			fPlayerInvisDist = pSettings->fPlayerInvisibleDistance * fDistScale * pSettings->fFadeDistanceScale;
		}

		if (pSettings->bIsInInspectMode)
		{
			F32 fDiff = fPlayerFadeDist - fPlayerInvisDist;

			fPlayerFadeDist *= 3.0f; //TODO put in settings -DHOGBERG
			fPlayerInvisDist = fPlayerFadeDist - fDiff;
		}

		if (fDistance < fPlayerFadeDist || fDistance < fPlayerInvisDist)
		{
			if (fDistance > fPlayerInvisDist)
			{
				pEnt->fCameraCollisionFade = (fDistance-fPlayerInvisDist)/(fPlayerFadeDist-fPlayerInvisDist);
				pEnt->fCameraCollisionFade *= pEnt->fCameraCollisionFade;
				// Don't change no interp, it can be on for other unrelated reasons
			}
			else
			{
				pEnt->bNoInterpAlpha = true;
				pEnt->fCameraCollisionFade = 0.0f;
			}
		}
		else
		{
			// Don't change no interp, it can be on for other unrelated reasons
			pEnt->fCameraCollisionFade = 1.0f;
		}
	}
}

void gclCamera_DoEntityCollisionFade(Entity* pIgnoreEnt, Vec3 vCameraPos) {
	Entity *pEnt;
	EntityIterator *iter = entGetIteratorAllTypesAllPartitions(0,ENTITYFLAG_IS_PLAYER);
	F32 fDist = 0;

	while(pEnt = EntityIteratorGetNext(iter)) {
		if (pEnt == pIgnoreEnt || entCheckFlag(pEnt, ENTITYFLAG_DONOTDRAW)) {
			// Do nothing
		} else if (dtSkeletonIsRagdoll(pEnt->dyn.guidSkeleton)) {
			//for now don't collision fade a ragdolled client side body when it's blocking the camera's view.
			//There's quite a bit of coding required to do this properly in the entGetDistance function (which
			//is based on changing the body capsules to use the ragdoll's physics simulation data on the game
			//client). Without disabling this here, any ragdoll bodies could visually pop in & out of view based
			//on their entities position when they first died (before tumbling down stairs or falling off a ledge)
			pEnt->fCameraCollisionFade = 1.0f;
		} else {
			Vec3 vCamDir, vEntSphereCenter;
			F32 fEntFadeDist = 1.5f;
			F32 fEntSphereRadius = entGetBoundingSphere(pEnt, vEntSphereCenter);
			F32 fCheckDist = fEntSphereRadius + fEntFadeDist;

			subVec3(vCameraPos, vEntSphereCenter, vCamDir);
			
			if (lengthVec3Squared(vCamDir) < fCheckDist * fCheckDist) {
				fDist = entGetDistance(NULL, vCameraPos, pEnt, NULL, NULL);
				if (fDist < fEntFadeDist) {
					pEnt->fCameraCollisionFade = 0.0f;
				} else {
					pEnt->fCameraCollisionFade = 1.0f;
				}
			} else {
				pEnt->fCameraCollisionFade = 1.0f;
			}
		}
	}
	EntityIteratorRelease(iter);
}

static void getNewCameraShakeGoals(const GfxCameraController *pCamera, Vec3 vPYROut, Vec3 vXYZOut) {

	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );

	// Adjust the camera by a random amount
	int i;

	// First do panning
	if (pSettings->fCameraShakePan > 0.0f)
	{
		Vec3 vBias;
		unitVec3(vBias);

		if (pSettings->fCameraShakeVertical > 0.0f) // more vertical than horizontal
		{
			vBias[0] = (1.0f - pSettings->fCameraShakeVertical);
		}
		else if (pSettings->fCameraShakeVertical < 0.0f) // more horizontal than vertical
		{
			vBias[1] = (1.0f + pSettings->fCameraShakeVertical);
		}

		for(i=0; i<3; i++)
		{
			vXYZOut[i] = 10 * SIGN(randomF32()) * pSettings->fMaxShake * pSettings->fCameraShakeMagnitude * pSettings->fCameraShakePan * vBias[i];
		}
	}
	else
		zeroVec3(vXYZOut);

	// Now do rotation
	if (pSettings->fCameraShakePan < 1.0f)
	{
		Vec3 vBias;
		unitVec3(vBias);

		if (pSettings->fCameraShakeVertical > 0.0f) // more vertical than horizontal
		{
			vBias[1] = (1.0f - pSettings->fCameraShakeVertical);
		}
		else if (pSettings->fCameraShakeVertical < 0.0f) // more horizontal than vertical
		{
			vBias[0] = (1.0f + pSettings->fCameraShakeVertical);
		}

		for(i=0; i<3; i++)
		{
			vPYROut[i] = SIGN(randomF32()) * pSettings->fMaxShake * pSettings->fCameraShakeMagnitude * (pSettings->fCameraShakePan-1.0f) * vBias[i];
		}
	}
	else
		zeroVec3(vPYROut);

}

void gclCamera_GetShake(const GfxCameraController *pCamera, Vec3 vPYROut, Vec3 vXYZOut, F32 fElapsedTime)
{
	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );

	float fSpeedScale = pSettings->fCameraShakeSpeed < 0 ? 0 : pSettings->fCameraShakeSpeed * 10;

	float fShakeSpeed = 10  * fSpeedScale;
	float fShakeAccel = 0.5 * fSpeedScale;

	if (pSettings->fCameraShakeTime > 0.0f) {

		if(fSpeedScale == 10) {
			getNewCameraShakeGoals(pCamera, vPYROut, vXYZOut);
			addVec3(pCamera->campyr, vPYROut, vPYROut);
			zeroVec3(pSettings->vCameraShakeCurrentPYR);
			zeroVec3(pSettings->vCameraShakeCurrentXYZ);
			pSettings->fCameraShakeTime -= fElapsedTime;
			return;
		}

		if(pSettings->fCameraShakeShiftTime <= 0) {

			getNewCameraShakeGoals(pCamera,
				pSettings->vCameraShakeDestPYR,
				pSettings->vCameraShakeDestXYZ);

			// We know it's going to overshoot, so scale down a bit.
			scaleVec3(pSettings->vCameraShakeDestPYR, 0.5, pSettings->vCameraShakeDestPYR);

			if(fShakeSpeed) {
				pSettings->fCameraShakeShiftTime = 1.0;
			}

		}

		pSettings->fCameraShakeShiftTime -= fShakeSpeed * fElapsedTime * 50;

		{
			int i;

			// Get the delta to the dest position.
			Vec3 pyrdelta;
			Vec3 xyzdelta;
			subVec3(pSettings->vCameraShakeDestPYR, pSettings->vCameraShakeCurrentPYR, pyrdelta);
			subVec3(pSettings->vCameraShakeDestXYZ, pSettings->vCameraShakeCurrentXYZ, xyzdelta);

			// Accelerate in that direction.
			scaleAddVec3(pyrdelta, fShakeAccel, pSettings->vCameraShakePYRVel, pSettings->vCameraShakePYRVel);
			scaleAddVec3(xyzdelta, fShakeAccel, pSettings->vCameraShakeXYZVel, pSettings->vCameraShakeXYZVel);
			
			// Do some arbitrary dampening.
			scaleVec3(pSettings->vCameraShakePYRVel, 0.99, pSettings->vCameraShakePYRVel);
			scaleVec3(pSettings->vCameraShakeXYZVel, 0.99, pSettings->vCameraShakeXYZVel);

			// Add velocity to position.
			addVec3(pSettings->vCameraShakePYRVel, pSettings->vCameraShakeCurrentPYR, pSettings->vCameraShakeCurrentPYR);
			addVec3(pSettings->vCameraShakeXYZVel, pSettings->vCameraShakeCurrentXYZ, pSettings->vCameraShakeCurrentXYZ);

			// Clamp position to something sane.
			for(i = 0; i < 3; i++) {

				if(fabs(pSettings->vCameraShakeCurrentPYR[i]) > fabs(pSettings->vCameraShakeDestPYR[i]) * 4) {
					pSettings->vCameraShakeCurrentPYR[i] =
						fabs(pSettings->vCameraShakeDestPYR[i]) * 4 *
						SIGN(pSettings->vCameraShakeCurrentPYR[i]);
				}

				if(fabs(pSettings->vCameraShakeCurrentXYZ[i]) > fabs(pSettings->vCameraShakeDestXYZ[i]) * 4) {
					pSettings->vCameraShakeCurrentXYZ[i] =
						fabs(pSettings->vCameraShakeDestXYZ[i]) * 4 *
						SIGN(pSettings->vCameraShakeCurrentXYZ[i]);
				}

			}

			// Add position as the offset.
			addVec3(pCamera->campyr, pSettings->vCameraShakeCurrentPYR, vPYROut);
			copyVec3(pSettings->vCameraShakeCurrentXYZ, vXYZOut);
		}

		pSettings->fCameraShakeTime -= fElapsedTime;
	}
	else
	{
		addVec3(pCamera->campyr, pSettings->vCameraShakeCurrentPYR, vPYROut);
		zeroVec3(pSettings->vCameraShakePYRVel);

		// So we don't immediately snap to the unshaken orientation,
		// just gradually return to normal.
		scaleVec3(pSettings->vCameraShakeCurrentPYR, 0.99, pSettings->vCameraShakeCurrentPYR);

		zeroVec3(vXYZOut);
	}
}

static F32 gclCamera_GetNearOffsetMaxRange( CameraSettings *pSettings )
{
	switch (pSettings->eMode)
	{
		xcase kCameraMode_AimCamera:
		{
			AimCameraSettings* pAimSettings = (AimCameraSettings*)pSettings->pModeSettings;
			if (pAimSettings->fSavedDistance > pSettings->v2NearOffsetRanges[1])
			{
				return pAimSettings->fSavedDistance;
			}
		}
		xcase kCameraMode_ShooterCamera:
		{
			ShooterCameraSettings* pShooterSettings = (ShooterCameraSettings*)pSettings->pModeSettings;
			if (pShooterSettings->fNearOffsetRange > FLT_EPSILON)
			{
				return pShooterSettings->fNearOffsetRange;
			}
		}
	}
	return pSettings->v2NearOffsetRanges[1];
}

static F32 gclCamera_GetNearOffsetScreen(CameraSettings* pSettings, Entity* pEnt)
{
	F32 fNearOffset = pSettings->fNearOffsetScreen;

	if (pSettings->bIsInAimMode)
	{
		switch (pSettings->eMode)
		{
			xcase kCameraMode_ShooterCamera:
			{
				ShooterCameraSettings* pShooterSettings = (ShooterCameraSettings*)pSettings->pModeSettings;
				if (pShooterSettings->fNearOffsetAim > FLT_EPSILON)
				{
					fNearOffset = pShooterSettings->fNearOffsetAim;
				}
			}
			xcase kCameraMode_AimCamera:
			{
				AimCameraSettings* pAimSettings = (AimCameraSettings*)pSettings->pModeSettings;
				if (pAimSettings->fNearOffsetAim > FLT_EPSILON)
				{
					fNearOffset = pAimSettings->fNearOffsetAim;
				}
			}
		}
	}
	if (pSettings->bAdjustOffsetsByCapsuleSize && 
		pSettings->fDefaultCapsuleRadius > FLT_EPSILON)
	{
		const Capsule*const* eaCapsules;
		if (mmGetCapsules(SAFE_MEMBER(pEnt, mm.movement), &eaCapsules))
		{
			F32 fRadius = 0.0f;
			S32 i;
			for (i = 0; i < eaSize(&eaCapsules); i++)
			{
				if (eaCapsules[i]->fRadius > fRadius)
				{
					fRadius = eaCapsules[i]->fRadius;
				}
			}
			if (fRadius > pSettings->fDefaultCapsuleRadius)
			{
				fNearOffset *= (fRadius / pSettings->fDefaultCapsuleRadius);
			}
		}
	}
	return fNearOffset;
}

static void gclCamera_GetNearOffset(GfxCameraController* pCamera, Entity* pEnt, Vec3 vPYR, F32* pfX, F32* pfZ)
{
	static F32 s_fLastOffset = 0.0f;
	CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
	F32 fNearOffset, fAngleDelta = 0.0f;
	Vec2 vFacePY;

	if (!gclCamera_ShouldTurnToFace() && !pSettings->bIgnoreCamPYRForNearOffset)
	{
		entGetFacePY(pEnt,vFacePY);
		gclCamera_ConvertEntityPYToCamPy(vFacePY,vFacePY);
		fAngleDelta = ABS(subAngle(vPYR[1],vFacePY[1]));
	}
	if (pSettings->bUseNearOffset && fAngleDelta < HALFPI && pCamera->camdist > FLT_EPSILON)
	{
		F32 fNearOffsetScreen = gclCamera_GetNearOffsetScreen(pSettings, pEnt);
		F32 fNearScale;
		F32 fMinRange = MAX(gclCamera_GetClosestDistance(pSettings), pSettings->v2NearOffsetRanges[0]);
		F32 fMaxRange = gclCamera_GetNearOffsetMaxRange(pSettings);
		F32 fAspect = gfxGetAspectRatio();
		F32 fHalfFOVy = gclCamera_GetFOVy(pCamera) * 0.5f;
		F32 fSWx = 2 * MINF(fMinRange, pCamera->camdist) * fAspect * tan(fHalfFOVy);
		F32 fWorldOffset = fNearOffsetScreen * fSWx;
		F32 fRangeInterval = MAXF(fMaxRange-fMinRange, FLT_EPSILON);

		fNearScale = CLAMPF32((pSettings->fDistance - fMinRange)/fRangeInterval, 0.0f, 1.0f);
		fNearOffset = interpF32(fNearScale, fWorldOffset, 0.0f);
		if (!pSettings->bIgnoreCamPYRForNearOffset)
		{
			fNearOffset = fNearOffset * cos(vPYR[0]);
			fNearOffset = interpF32(fAngleDelta/HALFPI,fNearOffset,0.0f);
		}
	}
	else
	{
		fNearOffset = 0;
	}

	(*pfX) = -cos(vPYR[1]) * (pSettings->fOffset + fNearOffset);
	(*pfZ) = sin(vPYR[1]) * (pSettings->fOffset + fNearOffset);
}

#define CAMERA_SNAPTO_TIMESTEP 0.03f

static F32 gclCamera_FixAngle(F32 fAngle, F32 fAngleRelative)
{
	while (fAngle - fAngleRelative > PI)
		fAngle -= TWOPI;
	while (fAngle - fAngleRelative <= -PI)
		fAngle += TWOPI;

	return fAngle;
}

static void gclCamera_SnapToRotate(GfxCameraController *pCamera, 
								   SnapToSettings* pSnapSettings,
								   Vec2 vTargetPY, F32 fElapsedTime,
								   bool bIgnoreYaw)
{	
	Vec2 vSmoothPY;
	F32 fSampleTime = pSnapSettings->Data.fHistorySampleTime;
	
	if (fSampleTime > FLT_EPSILON)
	{
		F32 fHistoryMultiplier = pSnapSettings->Data.fHistoryMultiplier;
		CameraSmoothNode* pNode = eaGet(&pSnapSettings->eaHistory, 0);
		if (pNode)
		{
			//Unroll yaw
			vTargetPY[1] = gclCamera_FixAngle(vTargetPY[1], pNode->vVal[1]);
		}
		gclCamera_GetSmoothDelta2(&pSnapSettings->eaHistory,
			fHistoryMultiplier,
			fSampleTime,
			fElapsedTime,
			vTargetPY,
			vSmoothPY);
	}
	else
	{
		copyVec2(vTargetPY, vSmoothPY);
	}

	if (pSnapSettings->bSnappedToTarget)
	{
		//When snapped to the target, follow the smoothed facing direction
		pCamera->targetpyr[0] = gclCamera_FixAngle(vSmoothPY[0], 0);
		if (!bIgnoreYaw)
		{
			pCamera->targetpyr[1] = gclCamera_FixAngle(vSmoothPY[1], 0);
		}
	}
	else //Tween to the target using an S-curve velocity
	{
		F32 fMaxAngle = RAD(pSnapSettings->Data.fMaxAngle);
		F32 fMinSpeed = RAD(pSnapSettings->Data.fMinSpeed);
		F32 fMaxSpeed = RAD(pSnapSettings->Data.fMaxSpeed);
		copyVec3(pCamera->campyr, pCamera->targetpyr);
		pSnapSettings->fAccum += fElapsedTime;
		while (pSnapSettings->fAccum >= CAMERA_SNAPTO_TIMESTEP)
		{
			Vec2 vDirPY;
			F32 fProgress, fSpeed, fAngle;
			copyVec3(pCamera->targetpyr, pCamera->campyr);
			vDirPY[0] = gclCamera_FixAngle(vSmoothPY[0] - pCamera->campyr[0], 0);
			vDirPY[1] = gclCamera_FixAngle(vSmoothPY[1] - pCamera->campyr[1], 0);
			fAngle = normalVec2(vDirPY);
			fProgress = CLAMPF(fAngle / fMaxAngle, 0.0f, 1.0f);
			fProgress = -2*powf(fProgress,3)+3*powf(fProgress,2);
			fSpeed = fMinSpeed + fProgress * (fMaxSpeed-fMinSpeed);
			fSpeed *= CAMERA_SNAPTO_TIMESTEP;

			if (fSpeed < fAngle)
			{
				scaleByVec2(vDirPY, fSpeed);
			}
			else
			{
				scaleByVec2(vDirPY, fAngle);
				pSnapSettings->bSnappedToTarget = true;
				pSnapSettings->fAccum = 0.0f;
			}
			pCamera->targetpyr[0] = addAngle(pCamera->campyr[0],vDirPY[0]);
			if (!bIgnoreYaw)
			{
				pCamera->targetpyr[1] = addAngle(pCamera->campyr[1],vDirPY[1]);
			}
			
			pSnapSettings->fAccum -= CAMERA_SNAPTO_TIMESTEP;
		}
	}
}

static void gclChaseCameraFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime, F32 fRealElapsedTime)
{
	Entity *pEnt = gclCamera_GetEntity();
	CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
	ChaseCameraSettings *pChaseSettings = (ChaseCameraSettings*)pSettings->pModeSettings;

	if (pEnt==NULL || pChaseSettings==NULL)
		return;

	if (pSettings->bIsMouseLooking)
	{
		U32 uiMouseLookDelay = pChaseSettings->fMouseLookDelay*1000;
		gclCamera_ResetSnapToSettings(&pChaseSettings->SnapData);
		pChaseSettings->uiNextChaseTime = gGCLState.totalElapsedTimeMs + uiMouseLookDelay;
		gclCamera_GetInput(pCamera, pCameraView, fRealElapsedTime);
	}
	else
	{
		mouseLock(0);
		if (pChaseSettings->uiNextChaseTime < gGCLState.totalElapsedTimeMs)
		{
			Vec2 vPlayerPY;
			entGetFacePY(pEnt, vPlayerPY);
			gclCamera_ConvertEntityPYToCamPy(vPlayerPY, vPlayerPY);
			
			gclCamera_SnapToRotate(pCamera, &pChaseSettings->SnapData, vPlayerPY, fRealElapsedTime, false);

			devassertmsgf(FINITEVEC3(pCamera->targetpyr), 
				"ChaseCam: Undefined pCamera PYR! (%f) (%f) (%f), PlayerPY: (%f) (%f)",
				pCamera->targetpyr[0], pCamera->targetpyr[1], pCamera->targetpyr[2],
				vPlayerPY[0], vPlayerPY[1]);
		}
	}
}

static F32 gclSpaceCameraLock_GetAutoFocusSpeedRatio(AutoTargetSettings* pAutoSettings)
{
	F32 fMinSpeedRatio = pAutoSettings->bFarFromDesiredPY ? 0.5f : 0.0f;
	F32 fTimeRatio = MAXF(pAutoSettings->fAutoFocusTimeleft, 0.0f)/pAutoSettings->fAutoFocusTime;
	F32 fSpeedRatio = interpF32(fTimeRatio, fMinSpeedRatio, 1.0f);
	return fSpeedRatio;
}

static void gclSpaceCameraLockFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime, F32 fRealElapsedTime)
{
	Entity *pEnt = gclCamera_GetEntity();
	Entity *pTargetEnt = entity_GetTarget(pEnt);
	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );
	AutoTargetSettings* pAutoSettings = (AutoTargetSettings*)pSettings->pModeSettings;
	F32 fAspect, fHalfFOVx, fHalfFOVy, fDampenAngle, fTargetDist;
	S32 iWidth, iHeight;
	Vec3 vTargetPos, vTargetVel, vTargetDir;
	Vec2 vPlayerPY, vTargetPY, vIdealTargetPY;
	bool bTweenToIdeal = false;

	if (!pEnt || !pEnt->pChar || !pEnt->pPlayer || pSettings->eMode != kCameraMode_AutoTarget || gbNoGraphics)
		return;
	
	PERFINFO_AUTO_START_FUNC();
	
	gfxGetActiveSurfaceSize(&iWidth, &iHeight);

	//compute fov parameters
	fAspect = gfxGetAspectRatio();
	fHalfFOVy = gclCamera_GetFOVy(pCamera) * 0.5f;
	fHalfFOVx = atan( fAspect * tan( fHalfFOVy ) );
	fDampenAngle = MAX(fHalfFOVx,fHalfFOVy)*2;

	//entity facing direction
	entGetFacePY(pEnt, vPlayerPY);
	gclCamera_ConvertEntityPYToCamPy(vPlayerPY, vPlayerPY);

	//if the player is holding down a mouse button, then don't do rotations
	if (mouseDown(MS_LEFT) || mouseDown(MS_RIGHT))
	{
		gclSpaceCameraLock_Reset(pSettings);
	}
	
	if (pTargetEnt && entIsAlive(pTargetEnt))
	{
		bool bUseIdealPY = true;
		bool bTargetHasEntGen = pTargetEnt->pEntUI && pTargetEnt->pEntUI->pGen; 
		F32 fYawDiff, fPitchDiff, fYawAbs, fPitchAbs, fYawAdjust, fPitchAdjust, fDotRight, fDotUp;
		Vec3 vCamPos;
		F32 fCM = pAutoSettings->fCenterMultiplier;
		F32 fPadding = 1/40.0f;
		F32 fWRatio = bTargetHasEntGen ? MINF(CBoxWidth(&pTargetEnt->pEntUI->pGen->ScreenBox)/(F32)iWidth,0.5f) : 0.5f;
		F32 fHRatio = bTargetHasEntGen ? MINF(CBoxHeight(&pTargetEnt->pEntUI->pGen->ScreenBox)/(F32)iHeight,0.5f) : 0.5f;
		F32 fHalfYawPadded = (1 - fWRatio - fPadding) * fHalfFOVx;
		F32 fHalfPitchPadded = (1 - fHRatio - fPadding) * fHalfFOVy;

		if ((pAutoSettings->eFlags & kAutoCamLockFlags_HadTargetLastFrame)==0)
		{
			gclSpaceCameraLock_Reset(pSettings);
			pAutoSettings->eFlags |= (kAutoCamLockFlags_HadTargetLastFrame
									 |kAutoCamLockFlags_MinimalRotation);
			pAutoSettings->SnapTo.Data = pAutoSettings->SnapToMinimum;
			pAutoSettings->SnapTo.bSnappedToTarget = false;
			pAutoSettings->fAutoFocusTimeleft = 0.0f;
			pAutoSettings->bFarFromDesiredPY = false;
			pAutoSettings->bSetInitialTargetPY = true;
		}

		if (pAutoSettings->fDelay < pAutoSettings->fMouseDelayTarget)
		{
			pAutoSettings->fDelay += fRealElapsedTime;
			PERFINFO_AUTO_STOP();
			return;
		}

		//Adjust the tweening logic when moving into or out of combat 
		if (entIsInCombat(pEnt))
		{
			if (!(pAutoSettings->eFlags & kAutoCamLockFlags_IsInCombat))
			{
				pAutoSettings->SnapTo.Data = pAutoSettings->SnapToCombat;
				pAutoSettings->SnapTo.bSnappedToTarget = false;
				pAutoSettings->eFlags |= kAutoCamLockFlags_IsInCombat;
			}	
		}
		else
		{
			if (pAutoSettings->eFlags & kAutoCamLockFlags_IsInCombat)
			{
				if (pAutoSettings->eFlags & kAutoCamLockFlags_MinimalRotation)
				{
					pAutoSettings->SnapTo.Data = pAutoSettings->SnapToMinimum;
					pAutoSettings->SnapTo.bSnappedToTarget = false;
				}
				else
				{
					pAutoSettings->SnapTo.Data = pAutoSettings->SnapToTarget;
					pAutoSettings->SnapTo.bSnappedToTarget = false;
				}
				pAutoSettings->eFlags &= ~kAutoCamLockFlags_IsInCombat;
			}
		}

		gfxGetActiveCameraPos(vCamPos);
		entGetPos(pTargetEnt, vTargetPos);
		entCopyVelocityFG(pTargetEnt, vTargetVel);
		scaleByVec3(vTargetVel, fRealElapsedTime);
		addVec3(vTargetPos, vTargetVel, vTargetPos);
		subVec3(vTargetPos, vCamPos, vTargetDir);
		fTargetDist = normalVec3(vTargetDir);
		getVec3YP(vTargetDir,&vTargetPY[1],&vTargetPY[0]);
		gclCamera_ConvertEntityPYToCamPy(vTargetPY, vTargetPY);
		copyVec2(vTargetPY,vIdealTargetPY);
		fPitchDiff = subAngle(vTargetPY[0],pCamera->campyr[0]);
		fYawDiff = subAngle(vTargetPY[1],pCamera->campyr[1]);
		fPitchAbs = ABS(fPitchDiff);
		fYawAbs = ABS(fYawDiff);
		fDotRight = cosf(subAngle(vTargetPY[1], addAngle(vPlayerPY[1],HALFPI)));
		fDotUp = cosf(subAngle(vTargetPY[0], subAngle(vPlayerPY[0],HALFPI)));
		fPitchAdjust = fHalfPitchPadded * fDotUp * 0.65f * fCM  + fHalfPitchPadded * 0.35f * fCM;
		fYawAdjust = fHalfYawPadded * fDotRight * fCM;
		vIdealTargetPY[0] += fPitchAdjust;
		vIdealTargetPY[1] -= fYawAdjust;

		if (pAutoSettings->bSetInitialTargetPY)
		{
			copyVec2(vTargetPY, pAutoSettings->vInitialTargetPY);
			pAutoSettings->bSetInitialTargetPY = false;
		}

		if (pAutoSettings->eFlags & kAutoCamLockFlags_MinimalRotation)
		{
			if (fPitchAbs > fHalfFOVy*1.5f || fYawAbs > fHalfFOVx*1.5f)
			{
				pAutoSettings->eFlags &= ~kAutoCamLockFlags_MinimalRotation;
				if (pAutoSettings->eFlags & kAutoCamLockFlags_IsInCombat)
				{
					pAutoSettings->SnapTo.Data = pAutoSettings->SnapToCombat;
				}
				else
				{
					pAutoSettings->SnapTo.Data = pAutoSettings->SnapToTarget;
				}
				pAutoSettings->SnapTo.bSnappedToTarget = false;
			}
			else //Gradually drift towards the ideal facing direction
			{
				bool bMoving = true;
				bool bOffscreen = fPitchAbs > fHalfPitchPadded || fYawAbs > fHalfYawPadded;
				F32 fMinSpeedTarget = pAutoSettings->fAutoFocusMinSpeedTarget;
				Vec2 vMovePY;
				subVec2(vTargetPY, pAutoSettings->vInitialTargetPY, vMovePY);

				if (pEnt->pPlayer->fMovementThrottle >= pAutoSettings->fAutoFocusMinThrottle
					|| lengthVec2(vTargetVel) > fMinSpeedTarget*fRealElapsedTime)
				{
					bMoving = true;
				}

				if (!pAutoSettings->bFarFromDesiredPY && 
					pAutoSettings->fAutoFocusTimeleft <= 0.0f)
				{
					bool bFarFromDesiredPY = false;
					if (lengthVec2(vMovePY) > RAD(pAutoSettings->fAutoFocusAngle))
					{
						bFarFromDesiredPY = true;
					}
					if (bOffscreen || (bMoving && bFarFromDesiredPY))
					{
						pAutoSettings->fAutoFocusTimeleft = pAutoSettings->fAutoFocusTime;
						pAutoSettings->bFarFromDesiredPY = bFarFromDesiredPY;
					}
				}
				if (!bMoving)
				{
					copyVec2(vTargetPY, pAutoSettings->vInitialTargetPY);
					if (pAutoSettings->SnapTo.bSnappedToTarget &&
						pAutoSettings->fAutoFocusTimeleft <= 0.0f)
					{
						pAutoSettings->SnapTo.bSnappedToTarget = false;
					}
				}
				if (pAutoSettings->bFarFromDesiredPY || pAutoSettings->fAutoFocusTimeleft > 0.0f)
				{
					F32 fMoveAngle;
					F32 fSpeedMult = bOffscreen ? pAutoSettings->fAutoFocusOffscreenMult : 1;
					F32 fSpeedRatio = gclSpaceCameraLock_GetAutoFocusSpeedRatio(pAutoSettings);
					F32 fSpeed = RAD(pAutoSettings->fAutoFocusSpeed) * fSpeedRatio * fSpeedMult;
					F32 fSpeedFrame = fSpeed * fRealElapsedTime;
					F32 fPitchAdj = subAngle(vIdealTargetPY[0],pCamera->campyr[0]);
					F32 fYawAdj = subAngle(vIdealTargetPY[1],pCamera->campyr[1]);
					if (!bOffscreen)
					{
						pAutoSettings->fAutoFocusTimeleft -= fRealElapsedTime;
					}
					bUseIdealPY = false;
					setVec2(vMovePY, fPitchAdj, fYawAdj);
					fMoveAngle = normalVec2(vMovePY);

					if (fSpeedFrame < fMoveAngle)
					{
						vTargetPY[0] = addAngle(pCamera->campyr[0], vMovePY[0]*fSpeedFrame); 
						vTargetPY[1] = addAngle(pCamera->campyr[1], vMovePY[1]*fSpeedFrame); 
					}
					else
					{
						///If close to the ideal PY, switch to auto-rotation
						pAutoSettings->eFlags &= ~kAutoCamLockFlags_MinimalRotation; 
						if (pAutoSettings->eFlags & kAutoCamLockFlags_IsInCombat)
						{
							pAutoSettings->SnapTo.Data = pAutoSettings->SnapToCombat;
						}
						else
						{
							pAutoSettings->SnapTo.Data = pAutoSettings->SnapToTarget;
						}
						copyVec2(pCamera->campyr, vTargetPY);
					}
				}
				else
				{
					PERFINFO_AUTO_STOP();
					return;
				}
			}
		}
		if (bUseIdealPY) //adjust target rotation to the ideal PY
		{
			copyVec2(vIdealTargetPY, vTargetPY);
		}
	}
	else if (g_CurrentScheme.bTargetLockCamDisableWhenNoTarget)
	{
		PERFINFO_AUTO_STOP();
		return;
	}
	else
	{
		const F32 fCloseAngle = RAD(pAutoSettings->fFollowCloseAngle);
		F32 fPitchAbs = ABS(subAngle(vPlayerPY[0],pCamera->campyr[0]));
		F32 fYawAbs = ABS(subAngle(vPlayerPY[1],pCamera->campyr[1]));

		if (pAutoSettings->eFlags & kAutoCamLockFlags_HadTargetLastFrame)
		{
			gclSpaceCameraLock_Reset(pSettings);
			pAutoSettings->eFlags &= ~kAutoCamLockFlags_MinimalRotation;
		}
		if (!(pAutoSettings->eFlags & kAutoCamLockFlags_TweenToHeading)
			&& (pAutoSettings->eFlags & kAutoCamLockFlags_HasMouseRotated)
			&& fYawAbs < fCloseAngle && fPitchAbs < fCloseAngle)
		{
			gclSpaceCameraLock_Reset(pSettings);
			pAutoSettings->SnapTo.Data = pAutoSettings->SnapToFollow;
			pAutoSettings->SnapTo.bSnappedToTarget = false;
			pAutoSettings->eFlags |= kAutoCamLockFlags_TweenToHeading;
			pAutoSettings->fDelay = pAutoSettings->fMouseDelayNoTarget+0.01f;
		}
		pAutoSettings->eFlags &= ~kAutoCamLockFlags_HasMouseRotated;

		if (pAutoSettings->fDelay < pAutoSettings->fMouseDelayNoTarget)
		{
			pAutoSettings->fDelay += fRealElapsedTime;
			PERFINFO_AUTO_STOP();
			return;
		}

		copyVec2(vPlayerPY, vTargetPY);
	}

	if (pTargetEnt || (pAutoSettings->eFlags & kAutoCamLockFlags_TweenToHeading))
	{
		gclCamera_SnapToRotate(pCamera, &pAutoSettings->SnapTo, vTargetPY, fRealElapsedTime, false);
	}

	devassertmsgf(FINITEVEC3(pCamera->targetpyr), 
		"AutoTarget: Undefined pCamera PYR! (%f) (%f) (%f)",
		pCamera->targetpyr[0], pCamera->targetpyr[1], pCamera->targetpyr[2]);

	PERFINFO_AUTO_STOP();
}


static void gclAutoTargetFunc(	GfxCameraController *pCamera, 
								GfxCameraView *pCameraView, 
								CameraSettings *pSettings, 
								Entity *pEnt,
								F32 fElapsedTime,
								F32 fRealElapsedTime)
{
	AutoTargetSettings* pAutoSettings = (AutoTargetSettings*)pSettings->pModeSettings;
	if (!pSettings->bIsMouseLooking)
	{
		pAutoSettings->eFlags &= ~kAutoCamLockFlags_IsTrackingRotate;
		gclSpaceCameraLockFunc(pCamera, pCameraView, fElapsedTime, fRealElapsedTime);

		if (!gfxGetFullscreen())
		{
			ClipCursor(NULL);
		}

		mouseLock(0);
	}
	else
	{
		pAutoSettings->eFlags |= kAutoCamLockFlags_HasMouseRotated;
		if ((pAutoSettings->eFlags & kAutoCamLockFlags_IsTrackingRotate) == 0)
		{
			copyVec2(pCamera->campyr, pAutoSettings->vRotatePY);
			pAutoSettings->eFlags |= kAutoCamLockFlags_IsTrackingRotate;
		}

		if (	pEnt->pChar->currentTargetRef != 0
			&&	(pAutoSettings->eFlags & kAutoCamLockFlags_IsTrackingRotate))
		{
			Vec2 vRotateDiff;
			subVec2(pCamera->campyr, pAutoSettings->vRotatePY, vRotateDiff);

			if (lengthVec2Squared(vRotateDiff) > RAD(1))
			{
				pAutoSettings->eFlags |= kAutoCamLockFlags_MinimalRotation;
				pAutoSettings->SnapTo.Data = pAutoSettings->SnapToMinimum;
				pAutoSettings->SnapTo.bSnappedToTarget = false;
				pAutoSettings->fAutoFocusTimeleft = 0.0f;
				pAutoSettings->bFarFromDesiredPY = false;
				pAutoSettings->bSetInitialTargetPY = true;
			}
		}

		gclSpaceCameraLock_Reset(pSettings);
		gclCamera_GetInput(pCamera, pCameraView, fRealElapsedTime);
	}
}

static void gclTweenToTargetCameraFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime, F32 fRealElapsedTime)
{
	Mat4 xCameraMatrix;
	Vec3 postCamFXpyr, postCamFXOffset;
	Entity *pEnt = gclCamera_GetEntity();
	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );
	TweenCameraSettings *pTweenSettings = (TweenCameraSettings*)pSettings->pModeSettings;
	bool bCloseDist = true;
	bool bCloseAngle = true;
	int i;	

	gclCamera_Interp(pCamera, fRealElapsedTime);

	if (pCamera->targetdist > 0.1f)
	{
		bCloseDist = false;
	}
	else
	{
		for (i = 0; i < 3; i++)
		{
			if (	pCamera->campyr[i] < pCamera->targetpyr[i] - 0.01f
				||	pCamera->campyr[i] > pCamera->targetpyr[i] + 0.01f)
			{
				bCloseAngle = false;
				break;
			}
		}
	}

	gclCamera_GetShake(pCamera, postCamFXpyr, postCamFXOffset, fRealElapsedTime);
	createMat3YPR(xCameraMatrix, postCamFXpyr);

	if (bCloseDist && bCloseAngle)
	{
		pCamera->camdist = pCamera->targetdist = pSettings->fDistance;
		gclCamera_SetLastMode(true,true);
		return;
	}

	if (!bCloseDist)
	{
		Vec3 vTargetDir;
		F32 fAllowedChange;
		F32 fIdealChange = pCamera->targetdist;
		F32 fMaxDistChange = fRealElapsedTime * pSettings->fDistanceInterpSpeed;

		if (ABS(fIdealChange) > fMaxDistChange)
		{
			fAllowedChange = fMaxDistChange * SIGN(fIdealChange);
		}
		else
		{
			fAllowedChange = fIdealChange;
		}

		copyVec3(pTweenSettings->vDir,vTargetDir);
		scaleByVec3(vTargetDir, fAllowedChange);

		addVec3(pCamera->camcenter,vTargetDir,pCamera->camcenter);

		pCamera->targetdist -= fAllowedChange;
	}

	{
		Vec3 vTemp;
		mulVecMat3(postCamFXOffset, xCameraMatrix, vTemp);
		addVec3(pCamera->camcenter, vTemp, xCameraMatrix[3]);
	}

	copyVec3(pCamera->camcenter, pCamera->camfocus);

	frustumSetCameraMatrix(&pCameraView->new_frustum, xCameraMatrix);
}

static F32 gclCamera_CalculatePitchToCenterOnReticle(	GfxCameraController *pCamera, 
														const Vec3 vEntPos, 
														const Vec3 vTargetPos)
{
	F32 fCameraPitch;
	Vec3 vToTargetPos, vEntOffsetPos;
	F32 fReticleVertNormOffset = 0.f;
	F32 fDistToTarget = distance3XZ(vEntPos, vTargetPos);
	gclReticle_GetReticleNormOffset(true, NULL, &fReticleVertNormOffset);
	
	copyVec3(vEntPos, vEntOffsetPos);
	
	vEntOffsetPos[1] += (0.5f - fReticleVertNormOffset) * 50.f;
	if (fDistToTarget > 10.f)
	{
		vEntOffsetPos[1] += ((fDistToTarget - 10.f) / 50.f) * 3.f;
	}
	vEntOffsetPos[1] += gclCamera_GetPlayerHeight(pCamera);

	subVec3(vEntOffsetPos, vTargetPos, vToTargetPos);
	fCameraPitch = getVec3Pitch(vToTargetPos);

	if (fCameraPitch > RAD(55.f))
		fCameraPitch = RAD(55.f);

	return fCameraPitch;
}

static void gclShooterCameraFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime, F32 fRealElapsedTime)
{
	Entity *pEnt = gclCamera_GetEntity();
	CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
	ShooterCameraSettings *pShooterSettings = (ShooterCameraSettings*)pSettings->pModeSettings;

	if (pEnt==NULL || pShooterSettings==NULL)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (gclPlayerControl_IsHardLockPressed())
	{
		Entity *pTarget = clientTarget_GetCurrentHardTargetEntity();

		if (pTarget)
		{
			F32 fYawDiff, fPitchDiff;
			Vec3 vEntPos, vToTargetEnt, vTargetPos;

			entGetPos(pEnt, vEntPos);
			entGetCombatPosDir(pTarget, NULL, vTargetPos, NULL);
			subVec3(vEntPos, vTargetPos, vToTargetEnt);
			
			pCamera->targetpyr[1] = getVec3Yaw(vToTargetEnt);
			
			pCamera->targetpyr[0] = gclCamera_CalculatePitchToCenterOnReticle(pCamera, vEntPos, vTargetPos);

			fPitchDiff = subAngle(pCamera->targetpyr[0], pCamera->campyr[0]);
			fYawDiff = subAngle(pCamera->targetpyr[1], pCamera->campyr[1]);
	
			if (pShooterSettings->bLockOnSnap || 
				(ABS(fYawDiff) <= RAD(pShooterSettings->fHardTargetSnapAngleThreshold) && 
				 ABS(fPitchDiff) <= RAD(pShooterSettings->fHardTargetSnapAngleThreshold)))
			{
				pShooterSettings->bLockOnSnap = true;
				pCamera->campyr[1] = pCamera->targetpyr[1];
			}

			g_bLockYaw = true;
			g_bLockPitch = true;
		}
		else
		{
			g_bLockYaw = false;
			g_bLockPitch = false;
			pShooterSettings->bLockOnSnap = false;
		}
	}
	else
	{
		g_bLockYaw = false;
		g_bLockPitch = false;
		pShooterSettings->bLockOnSnap = false;
	}
	
	if (pSettings->bIsInAimMode)
	{
		if (!pShooterSettings->bAimModeInit)
		{
			pShooterSettings->fSavedDistance = pSettings->fDistance;
			pSettings->fDistance = pShooterSettings->fAimDist;
			pShooterSettings->bAimModeInit = true;
		}
		if (pShooterSettings->fAimFOV > FLT_EPSILON)
		{
			gclCamera_SetFOV(pShooterSettings->fAimFOV);
		}
	}
	else
	{
		F32 fMinDist = pShooterSettings->fMinDist * pSettings->fZoomMult;
		F32 fMaxDist = fMinDist + (pShooterSettings->fMaxDist - pShooterSettings->fMinDist);

		if(pSettings->bIsInInspectMode)
			fMinDist = 5.0f; //TODO put in settings -DHOGBERG

		if (pShooterSettings->bAimModeInit)
		{
			pSettings->fDistance = pShooterSettings->fSavedDistance;
			pShooterSettings->bAimModeInit = false;
		}
		pSettings->fDistance = CLAMPF32(pSettings->fDistance, fMinDist, fMaxDist);
	}
	if (gclPlayerControl_IsAlwaysUsingMouseLookForced())
		gclPlayerControl_UpdateMouseInput(false, true);

	PERFINFO_AUTO_STOP();
}

static void gclAimCameraFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime, F32 fRealElapsedTime)
{
	Entity *pEnt = gclCamera_GetEntity();
	Entity *pTargetEnt = entity_GetTarget(pEnt);
	CameraSettings *pSettings = gclCamera_GetSettings( pCamera );
	AimCameraSettings *pAimSettings = (AimCameraSettings*)pSettings->pModeSettings;

	if (pEnt==NULL || pAimSettings==NULL)
		return;

	PERFINFO_AUTO_START_FUNC();

	if (pSettings->bIsMouseLooking)
	{
		if (pTargetEnt)
		{
			U32 uiMouseLookDelay = pAimSettings->fMouseLookDelay*1000;
			pAimSettings->uiNextAimTime = gGCLState.totalElapsedTimeMs + uiMouseLookDelay;
			gclCamera_ResetSnapToSettings(&pAimSettings->SnapData);
		}
	}
	else if (pAimSettings->uiNextAimTime < gGCLState.totalElapsedTimeMs)
	{
		bool bIgnoreYaw = false;
		Vec2 vDirPY;
		
		if (pTargetEnt)
		{
			Vec3 vEntPos, vTargetPos, vDir;
			entGetPos(pEnt, vEntPos);
			entGetPos(pTargetEnt, vTargetPos);
			subVec3(vTargetPos, vEntPos, vDir);
			getVec3YP(vDir, &vDirPY[1], &vDirPY[0]);
			gclCamera_ConvertEntityPYToCamPy(vDirPY, vDirPY);
			if (!pAimSettings->bHadTargetLastFrame)
			{
				gclCamera_ResetSnapToSettings(&pAimSettings->SnapData);
				pAimSettings->bHadTargetLastFrame = true;
			}
		}
		else
		{
			entGetFacePY(pEnt, vDirPY);
			gclCamera_ConvertEntityPYToCamPy(vDirPY, vDirPY);
			bIgnoreYaw = true;
			if (pAimSettings->bHadTargetLastFrame)
			{
				gclCamera_ResetSnapToSettings(&pAimSettings->SnapData);
				pAimSettings->bHadTargetLastFrame = false;
			}
		}

		gclCamera_SnapToRotate(pCamera, &pAimSettings->SnapData, vDirPY, fRealElapsedTime, bIgnoreYaw);

		devassertmsgf(FINITEVEC3(pCamera->targetpyr), 
			"AimCam: Undefined pCamera PYR! (%f) (%f) (%f), DirPY: (%f) (%f)",
			pCamera->targetpyr[0], pCamera->targetpyr[1], pCamera->targetpyr[2],
			vDirPY[0], vDirPY[1]);
	}
	
	PERFINFO_AUTO_STOP();
}


static void gclCamera_AdjustMouseSpeedFromFOV(GfxCameraController* pCamera)
{
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

	// When setting the field of view of the camera, also adjust mouse sensivity
	F32 fSpeedMult = (pCamera->projection_fov / gfxGetDefaultFOV());
	pSettings->pyrMouseSpeed[0] = pSettings->pyrDefaultMouseSpeed[0] * fSpeedMult;
	pSettings->pyrMouseSpeed[1] = pSettings->pyrDefaultMouseSpeed[1] * fSpeedMult;
}

#define EPSILON 0.0001f
static void gclCameraControllerUpdateProjectionFOV(GfxCameraController *pCamera, F32 fElapsedTime, F32 fRealElapsedTime)
{
	F32 fDiff;
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
	const F32 fFOVRate = pSettings->fFOVAdjustRate;
	if (!nearSameF32(pCamera->projection_fov, pCamera->target_projection_fov))
	{
		fDiff = pCamera->target_projection_fov - pCamera->projection_fov;
		if (fabsf(fDiff) > fFOVRate * fElapsedTime)
		{
			fDiff = SIGN(fDiff) * fFOVRate * fElapsedTime;
		}

		if (fabsf(fDiff) < EPSILON)
			pCamera->projection_fov = pCamera->target_projection_fov;
		else
			pCamera->projection_fov += fDiff;
	}
	if (pSettings->bScaleMouseSpeedByFOV)
	{
		gclCamera_AdjustMouseSpeedFromFOV(pCamera);
	}
	pCamera->target_projection_fov = pCamera->default_projection_fov; // must set every frame, otherwise it defaults to default_projection_fov
}

void gclCamera_SetFOV(F32 fFOV)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();

	if (pCamera && CAMERA_VALID_FOV(fFOV))
	{
		// Set the FOV for this frame
		pCamera->target_projection_fov = fFOV;
	}
}

// ------------------------------------------------------------------------------------------------------------------
void gclCamera_SetFxCameraMatrixOverride(Mat4 camera_matrix, bool bEnable, F32 fInfluence) 
{

	GfxCameraController* pCamera = gfxGetActiveCameraController();
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

	if(pSettings) 
	{
		copyMat4(camera_matrix, pSettings->xFXCameraMatrixOverride);
		pSettings->bUseFXCameraMatrixOverride = bEnable;
		pSettings->fFXCameraInfluence = fInfluence;
	}	
}

// ------------------------------------------------------------------------------------------------------------------
void gclCamera_DisableFocusSmoothOverride(bool bDisableImmediate)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
	if (pSettings)
	{
		CameraFocusOverride *pFocusOverride = &pSettings->focusOverride;

		if (!pFocusOverride->bEnabled)
			return;

		if (bDisableImmediate)
		{
			CameraFocusSmoothingData* pFocusSmoothing = pSettings->pFocusSmoothingData;

			pFocusOverride->bEnabled = false;

			if (pFocusSmoothing)
			{
				eaClearStruct(&pFocusSmoothing->eaPositionSmoothHistory, parse_CameraSmoothNode);
			}
		}
		else if (!pFocusOverride->bMovingToDisable)
		{
			Vec3 vCurFocusToDesired;
			F32 fDistToCam;
			
			subVec3(pFocusOverride->vLastDesiredCameraCenter, pFocusOverride->vCurFocusPosition, vCurFocusToDesired);
			fDistToCam = normalVec3(vCurFocusToDesired);

			pFocusOverride->bMovingToDisable = true;

			pFocusOverride->settings.fDistanceBasis = 1.f;
			pFocusOverride->settings.fSpeed = MAX(fDistToCam, 25.f);
			pFocusOverride->settings.fMinSpeed = pFocusOverride->settings.fSpeed * 0.5f;
			pFocusOverride->settings.fMaxSpeed = 0.f;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void gclCamera_EnableFocusOverrideInternal(	GfxCameraController* pCamera,
													CameraSettings* pSettings,
													F32 fSpeed, 
													F32 fDistanceBasis, 
													bool bLockOverride)
{
	if (pCamera && pSettings)
	{
		CameraFocusOverride *pFocusOverride = &pSettings->focusOverride;

		if (!pFocusOverride->bEnabled)
		{
			copyVec3(pCamera->camcenter, pFocusOverride->vLastDesiredCameraCenter);
			copyVec3(pCamera->camcenter, pFocusOverride->vCurFocusPosition);
		}

		pFocusOverride->bMovingToDisable = false;
		pFocusOverride->bEnabled = true;
		pFocusOverride->bResetOverride = false;
		pFocusOverride->bOverrideLocked = bLockOverride;

		pFocusOverride->settings.fSpeed = fSpeed;
		pFocusOverride->settings.fDistanceBasis = fDistanceBasis ? fDistanceBasis : 5.f;
	}
}

// ------------------------------------------------------------------------------------------------------------------
void gclCamera_EnableFocusOverride(F32 fSpeed, F32 fDistanceBasis)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);
	gclCamera_EnableFocusOverrideInternal(pCamera, pSettings, fSpeed, fDistanceBasis, false);
}

// ------------------------------------------------------------------------------------------------------------------
static void gclCamera_UpdateFocusOverride(F32 fElapsedTime, GfxCameraController* pCamera, CameraSettings* pSettings)
{
	CameraFocusOverride *pFocusOverride = &pSettings->focusOverride;
	Vec3 vCurFocusToDesired;
	F32 fDistToCam;
	F32 fSpeed, fScale, fStep;

	if (pFocusOverride->bResetOverride)
	{
		gclCamera_DisableFocusSmoothOverride(false);
	}

	copyVec3(pCamera->camcenter, pFocusOverride->vLastDesiredCameraCenter);

	if (pFocusOverride->bMovingToDisable)
	{
		if (distance3Squared(pCamera->camcenter, pFocusOverride->vCurFocusPosition) < SQR(0.5f))
		{
			gclCamera_DisableFocusSmoothOverride(true);
			return;
		}
	}

	subVec3(pCamera->camcenter, pFocusOverride->vCurFocusPosition, vCurFocusToDesired);
	fDistToCam = normalVec3(vCurFocusToDesired);
		
	fScale = fDistToCam / pFocusOverride->settings.fDistanceBasis;
	fSpeed = pFocusOverride->settings.fSpeed * fScale;
	if (fSpeed > 1000.f)
	{	// hard-coding a throttle to the speed. It's fairly arbitrary- so if we ever need to increase this, put it in a def.
		fSpeed = 1000.f;
	}

	fStep = fSpeed * fElapsedTime;
	if (fStep > fDistToCam)
		fStep = fDistToCam;

	scaleAddVec3(vCurFocusToDesired, fStep, pFocusOverride->vCurFocusPosition, pFocusOverride->vCurFocusPosition);

	copyVec3(pFocusOverride->vCurFocusPosition, pCamera->camcenter);

	if (!pFocusOverride->bOverrideLocked)
		pFocusOverride->bResetOverride = true;
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void gclCamera_OverrideCameraFocus(bool enable, F32 fSpeed, F32 fBasis)
{
	if (enable)
	{
		gclCamera_EnableFocusOverride(fSpeed, fBasis);
	}
	else
	{
		gclCamera_DisableFocusSmoothOverride(false);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void gclCamera_DisableLookatOverride(CameraSettings* pSettings)
{
	CameraLookatOverride *pLookatOverride = &pSettings->lookatOverride;

	pLookatOverride->pActivation = NULL;
	pLookatOverride->bHasCameraLookAtOverride = false;
	pLookatOverride->bMaintainCheck = false;
}

// --------------------------------------------------------------------------------------------------------------------
void gclCamera_SetLookatOverride(const Vec3 vPos, F32 fSpeed)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

	if (pSettings)
	{
		CameraLookatOverride *pLookatOverride = &pSettings->lookatOverride;
		// clear the struct
		memset(pLookatOverride, 0, sizeof(CameraLookatOverride) );
		
		pLookatOverride->bHasCameraLookAtOverride = true;
		copyVec3(vPos, pLookatOverride->vCameraLookatOverridePos);
		pLookatOverride->bSnapToTarget = (fSpeed < 0.f);
		pLookatOverride->bDisallowInputOverride = true;
		pLookatOverride->bUsePitch = true;
		pLookatOverride->bDisableLookatEachFrame = true;
		pLookatOverride->fCurOverrideSpeed = fSpeed;

		pLookatOverride->def.fObstructionDistNormalizedThreshold = 0.3f;
		pLookatOverride->def.fDisableLookatInputAngleThreshold = 6.f;
		pLookatOverride->def.fIgnoreInputAngleThreshold = 0.5f;

		if (pSettings->pDefaultLookatOverride)
		{
			CameraLookatOverrideDef *pLookatDef = pSettings->pDefaultLookatOverride;
			pLookatOverride->def.fDisableLookatInputAngleThreshold = pLookatDef->fDisableLookatInputAngleThreshold;
			pLookatOverride->def.fIgnoreInputAngleThreshold = pLookatDef->fIgnoreInputAngleThreshold;
			pLookatOverride->def.fObstructionDistNormalizedThreshold = pLookatDef->fObstructionDistNormalizedThreshold;
		}
	}
}

// --------------------------------------------------------------------------------------------------------------------
void gclCamera_SetPowerActivationLookatOverride(PowerActivation *pActivation)
{
	GfxCameraController* pCamera = gfxGetActiveCameraController();
	CameraSettings* pSettings = gclCamera_GetSettings(pCamera);

	if (pSettings)
	{
		if (pSettings->pPowerActivationMouseDamping)
		{
			pSettings->mouseDamping.pActivation = pActivation;
			pSettings->mouseDamping.bActive = true;
		}

		if (pSettings->pPowerActivationLookatOverride)
		{
			CameraLookatOverrideDef *pLookatOverrideDef = pSettings->pPowerActivationLookatOverride;
			CameraLookatOverride *pLookatOverride = &pSettings->lookatOverride;
			Entity *pCameraEnt = gclCamera_GetEntity();

			if (!pCameraEnt || !pActivation->erTarget || pActivation->erTarget == entGetRef(pCameraEnt))
				return;

			if (!pLookatOverride->bHasCameraLookAtOverride)
			{
				memset(pLookatOverride, 0, sizeof(CameraLookatOverride) );
				pLookatOverride->fCurOverrideSpeed = pLookatOverrideDef->fInterpStartSpeed;
			}
			else if (pLookatOverride->erEntity != pActivation->erTarget)
			{
				zeroVec2(pLookatOverride->vPYCameraOffset);
				pLookatOverride->bSnapToTarget = false;
				pLookatOverride->fTimeout = 0.f;
				pLookatOverride->fCurOverrideSpeed = pLookatOverrideDef->fInterpStartSpeed;
			}

			pLookatOverride->pActivation = pActivation;

			pLookatOverride->def = *pLookatOverrideDef;

			pLookatOverride->bDisallowInputOverride = false;
			
			if (pLookatOverrideDef->fMaintainRefreshPeriod > 0.f)
			{
				PowerDef *pDef = GET_REF(pActivation->hdef);
				if (pDef && pDef->eType == kPowerType_Maintained)
				{
					pLookatOverride->bMaintainCheck = true;
					pLookatOverride->fMaintainPeriod = pLookatOverrideDef->fMaintainRefreshPeriod;
				}
				else
				{
					pLookatOverride->bMaintainCheck = false;
				}
			}
			

			pLookatOverride->bHasCameraLookAtOverride = true;
			pLookatOverride->bSnapAfterMatching = true;
			pLookatOverride->erEntity = pActivation->erTarget;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static __forceinline bool isAngleWithinThreshold(F32 fAngle1, F32 fAngle2, F32 fThreshold)
{
	F32 fDiff = subAngle(fAngle1, fAngle2);
	return ABS(fDiff) <= fThreshold;
}

// ------------------------------------------------------------------------------------------------------------------
static bool gclCamera_IsCameraObstructedAtOrientation(	GfxCameraController* pCamera,
														const Vec3 vEntPos, 
														const Vec2 vPitchYaw,
														F32 fDistance)
{
	Vec3 vEntAdjustedPos;
	Vec3 vCameraPos;
	Vec3 vCamOffset;
	F32 fCamPlayerHeight = gclCamera_GetPlayerHeight(pCamera);

	copyVec3(vEntPos, vEntAdjustedPos);
	vEntAdjustedPos[1] += fCamPlayerHeight;
		
	sphericalCoordsToVec3(vCamOffset, vPitchYaw[1], -vPitchYaw[0] + HALFPI , fDistance);

	addVec3(vEntAdjustedPos, vCamOffset, vCameraPos);
	
	return worldCollideRay(PARTITION_CLIENT, vEntAdjustedPos, vCameraPos, WC_QUERY_BITS_CAMERA_BLOCKING, NULL);
}

// ------------------------------------------------------------------------------------------------------------------
static void gclCamera_UpdateCameraLookAtOverride(	GfxCameraController* pCamera, 
													CameraSettings* pSettings, 
													const Vec2 vCamMovePY, 
													bool bRotated, 
													F32 fElapsedTime)
{
	CameraLookatOverride *pLookatOverride = &pSettings->lookatOverride;

	if (!pLookatOverride->bHasCameraLookAtOverride && 
		pLookatOverride->bMaintainCheck && 
		pLookatOverride->pActivation)
	{
		Entity *pCameraEnt = gclCamera_GetEntity();

		if (pCameraEnt && pLookatOverride->pActivation == pCameraEnt->pChar->pPowActCurrent)
		{
			pLookatOverride->fMaintainPeriod -= fElapsedTime;
			if (pLookatOverride->fMaintainPeriod <= 0.f)
			{
				gclCamera_SetPowerActivationLookatOverride(pCameraEnt->pChar->pPowActCurrent);
			}
		}
		else
		{
			pLookatOverride->bMaintainCheck = false;
		}
	}
	
	// check if we have a camera look override target
	if (pLookatOverride->bHasCameraLookAtOverride)
	{
		Entity *pCameraEnt = gclCamera_GetEntity();
		Vec3 vLookatPos;
				
		if (!pCameraEnt || !pCameraEnt->pChar)
		{	
			gclCamera_DisableLookatOverride(pSettings);
			return;
		}


		// Get the look at position
		if (pLookatOverride->pActivation)
		{
			PowerActivation *pPowAct = NULL;
			Entity *pLookatEnt;

			// get our current power, or queued if the current isn't set
			if (pLookatOverride->pActivation == pCameraEnt->pChar->pPowActCurrent)
			{
				pPowAct = pCameraEnt->pChar->pPowActCurrent;
				pLookatOverride->bLockOutCameraLookatOverride = false;
			}
			else if (pLookatOverride->pActivation == pCameraEnt->pChar->pPowActQueued)
			{
				pPowAct = pCameraEnt->pChar->pPowActQueued;
				pLookatOverride->bLockOutCameraLookatOverride = false;
			}
			
			// if the power activation doesn't have a target, or is self casting, stop the lookat
			if (!pPowAct || !pPowAct->erTarget || pPowAct->erTarget == entGetRef(pCameraEnt))
			{
				gclCamera_DisableLookatOverride(pSettings);
				return;
			}
			
			// get the look at position from who we are targeting
			pLookatEnt = entFromEntityRefAnyPartition(pPowAct->erTarget);
			if (!pLookatEnt)
			{
				gclCamera_DisableLookatOverride(pSettings);
				return;
			}

			entGetCombatPosDir(pLookatEnt, NULL, vLookatPos, NULL);
		}
		else
		{
			copyVec3(pLookatOverride->vCameraLookatOverridePos, vLookatPos);
		}


		if (!pLookatOverride->bLockOutCameraLookatOverride)
		{
			Vec3 vCameraEntPos, vLookAtToCameraEnt;
			
			entGetPos(pCameraEnt, vCameraEntPos);
			subVec3(vCameraEntPos, vLookatPos, vLookAtToCameraEnt);

			addVec2(pLookatOverride->vPYCameraOffset, vCamMovePY, pLookatOverride->vPYCameraOffset);
			if (!pLookatOverride->bUsePitch)
				pLookatOverride->vPYCameraOffset[0] = 0.f;

			if (pLookatOverride->bDisallowInputOverride)
			{	// if bDisallowInputOverride is set, we're going to not allow the mouse to move the camera at all
				if (pLookatOverride->bUsePitch)
					pCamera->targetpyr[0] = pCamera->campyr[0] = subAngle(pCamera->campyr[0], vCamMovePY[0]);
				pCamera->targetpyr[1] = pCamera->campyr[1] = subAngle(pCamera->campyr[1], vCamMovePY[1]);
			}

			if (pLookatOverride->bDisallowInputOverride ||
				ABS(vCamMovePY[1]) < RAD(pLookatOverride->def.fIgnoreInputAngleThreshold * fElapsedTime))
			{
				pLookatOverride->fTimeout -= fElapsedTime;
				if (pLookatOverride->fTimeout <= 0)
				{
					pLookatOverride->fTimeout = 0.f;

					zeroVec2(pLookatOverride->vPYCameraOffset);
					pCamera->targetpyr[1] = getVec3Yaw(vLookAtToCameraEnt);

					// now that we have the target yaw, 
					// get the target pitch from around where our camera wants to be
					if (pLookatOverride->bUsePitch)
					{
						pCamera->targetpyr[0] = gclCamera_CalculatePitchToCenterOnReticle(pCamera, vCameraEntPos, vLookatPos);
					}

					if (pLookatOverride->def.fObstructionDistNormalizedThreshold)
					{
						F32 fDistanceThreshold = pSettings->fDistance * pLookatOverride->def.fObstructionDistNormalizedThreshold;
						// if the camera is obstructed at this location, don't move it
						if (gclCamera_IsCameraObstructedAtOrientation(	pCamera, 
																		vCameraEntPos, 
																		pCamera->targetpyr, 
																		fDistanceThreshold))
						{
							pCamera->targetpyr[0] = pCamera->campyr[0];
							pCamera->targetpyr[1] = pCamera->campyr[1];
						}
					}

					if (pLookatOverride->bSnapToTarget && 
						(!isAngleWithinThreshold(pCamera->campyr[0], pCamera->targetpyr[0], RAD(45.f)) ||
						 !isAngleWithinThreshold(pCamera->campyr[1], pCamera->targetpyr[1], RAD(45.f)))  )
					{
						pLookatOverride->bSnapToTarget = false;
					}
					
					if (pLookatOverride->bSnapToTarget)
					{
						pCamera->campyr[0] = pCamera->targetpyr[0];
						pCamera->campyr[1] = pCamera->targetpyr[1];
					}
					else
					{
						if (pLookatOverride->def.fInterpSpeedAccel && pLookatOverride->	def.fInterpMaxSpeed)
						{
							pLookatOverride->fCurOverrideSpeed += pLookatOverride->def.fInterpSpeedAccel * fElapsedTime;
							if (pLookatOverride->fCurOverrideSpeed > pLookatOverride->def.fInterpMaxSpeed)
							{
								pLookatOverride->fCurOverrideSpeed = pLookatOverride->def.fInterpMaxSpeed;
							}
						}

						if (pLookatOverride->fCurOverrideSpeed)
							gclCamera_SetPYInterpSpeed(pSettings, RAD(pLookatOverride->fCurOverrideSpeed));

						// check if we should snap and are matching the target PYR yet
						if (pLookatOverride->bSnapAfterMatching && 
							isAngleWithinThreshold(pCamera->campyr[0], pCamera->targetpyr[0], RAD(pLookatOverride->def.fSnapAngleThreshold)) && 
							isAngleWithinThreshold(pCamera->campyr[1], pCamera->targetpyr[1], RAD(pLookatOverride->def.fSnapAngleThreshold)))
						{
							pLookatOverride->bSnapToTarget = true;
						}
					}
				}
			}
			else
			{
				F32 fAngleThreshold;
				
				pLookatOverride->fTimeout = pLookatOverride->def.fLookatTimeout; 
				pLookatOverride->bSnapToTarget = false;

				if (!pLookatOverride->bDisallowInputOverride)
				{
					fAngleThreshold = RAD(pLookatOverride->def.fDisableLookatInputAngleThreshold);
					if (lengthVec2Squared(pLookatOverride->vPYCameraOffset) > SQR(fAngleThreshold))
					{
						if (!pLookatOverride->bMaintainCheck)
						{
							gclCamera_DisableLookatOverride(pSettings);
						}
						else
						{
							pLookatOverride->bHasCameraLookAtOverride = false;
						}
						return;
					}
				}
			}
		}
		else
		{	// the camera lookat was locked out due to input
			pLookatOverride->bLockOutCameraLookatOverride = true;
		}

		// primarily, when used by FX, disable the lookat each frame
		if (pLookatOverride->bDisableLookatEachFrame)
		{
			gclCamera_DisableLookatOverride(pSettings);
			return;
		}
	}
	else
	{	// release the cameraLookAt lockout for the next time the cameraLookAt gets overridden 
		pLookatOverride->bLockOutCameraLookatOverride = false;
	}
}

// --------------------------------------------------------------------------------------------------------------------
static void gclCamera_UpdateMouseInputDamping(	CameraSettings *pSettings, 
												GfxCameraController *pCamera, 
												F32 fElapsedTime,
												Vec2 vCamMovePYInOut) 
{
	if (pSettings->mouseDamping.bActive)
	{
		CameraTargetMouseDampingDef *pDef = pSettings->pPowerActivationMouseDamping;
		Entity *pEnt = gclCamera_GetEntity();
		Entity *pTarget = clientTarget_GetCurrentHardTargetEntity();
		CameraLookatOverride *pLookatOverride = &pSettings->lookatOverride;

		if (!pEnt || !pEnt->pChar)
		{
			pSettings->mouseDamping.bActive = false;
			pSettings->mouseDamping.bStartedDamping = false;
			return;
		}

		if (pEnt->pChar->pPowActCurrent == pSettings->mouseDamping.pActivation)
		{
			pSettings->mouseDamping.pCurrentActivation = pEnt->pChar->pPowActCurrent;
			pSettings->mouseDamping.bStartedDamping = true;
			pSettings->mouseDamping.fTimeout = pDef->fSecondsPostActivation;
		}
		else if (pSettings->mouseDamping.bStartedDamping && 
				 pSettings->mouseDamping.pCurrentActivation != pEnt->pChar->pPowActCurrent)
		{
			pSettings->mouseDamping.fTimeout -= fElapsedTime;
			if (pSettings->mouseDamping.fTimeout <= 0.f)
			{
				pSettings->mouseDamping.bActive = false;
				pSettings->mouseDamping.bStartedDamping = false;
				return;
			}
		}
		else if (!pEnt->pChar->pPowActCurrent && !pEnt->pChar->pPowActQueued)
		{
			pSettings->mouseDamping.bActive = false;
			pSettings->mouseDamping.bStartedDamping = false;
			return;
		}
		
		if (pTarget)
		{
			Vec3 vCameraEntPos, vLookAtToCameraEnt, vLookatPos;
			F32 fAngleDiff;
			Vec2 PYToTarget;

			if (pLookatOverride->bHasCameraLookAtOverride && !pLookatOverride->bLockOutCameraLookatOverride)
			{
				if (entGetRef(pTarget) == pLookatOverride->erEntity)
					return; 
			}


			entGetPos(pEnt, vCameraEntPos);
			entGetPos(pTarget, vLookatPos);
			subVec3(vCameraEntPos, vLookatPos, vLookAtToCameraEnt);

			PYToTarget[1] = getVec3Yaw(vLookAtToCameraEnt);

			fAngleDiff = subAngle(PYToTarget[1], pCamera->campyr[1]);
			fAngleDiff = ABS(fAngleDiff);
						
			// check if the mouse cursor is in the target box
			{
				CBox entityBox = {0};
				F32 fDistance = 0.f;
				S32 reticleX, reticleY;
				if (!entGetPrimaryCapsuleScreenBoundingBox(pTarget, &entityBox, &fDistance))
					return;
	
				gclReticle_GetReticlePosition(true, &reticleX, &reticleY);
				
				if (!CBoxIntersectsPoint(&entityBox, (F32)reticleX, (F32)reticleY))
					return;
			}


			if (fAngleDiff <= RAD(pDef->fOuterAngleThreshold))
			{
				F32 fDamp;

				if (fAngleDiff < RAD(pDef->fInnerAngleThreshold))
				{
					fDamp = pDef->fMouseDamp;
				}
				else
				{
					F32 fNorm = 1.f - (fAngleDiff - RAD(pDef->fInnerAngleThreshold)) / RAD(pDef->fOuterAngleThreshold);
					fDamp = 1.f - (1.f - pDef->fMouseDamp) * fNorm;
				}

				vCamMovePYInOut[1] *= fDamp;
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void gclDefaultCameraFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsedTime, F32 fRealElapsedTime)
{
	Mat4 xCameraMatrix;
	Vec3 v3Temp;
	Entity *pEnt = gclCamera_GetEntity();
	CameraSettings *pSettings = gclCamera_GetSettings(pCamera);
	Vec3 postCamFXPYR, postCamFXOffset;

	PERFINFO_AUTO_START_FUNC();

	if (!pEnt)
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	if (pSettings->eMode == kCameraMode_TweenToTarget)
	{
		gclTweenToTargetCameraFunc(pCamera, pCameraView, fElapsedTime, fRealElapsedTime);
		PERFINFO_AUTO_STOP();
		return;
	}

	gclSetCameraTarget(pEnt);

	if (!pCamera->lockedtotarget && pSettings->bInitialTargetLockHandled)
		pSettings->bInitialTargetLockHandled = false;
	
	if (pSettings->bResetCameraWhenAlive && !entCheckFlag(pEnt, ENTITYFLAG_DEAD))
		gclCamera_Reset();

	if(mrSurfaceDoCameraShake(SAFE_MEMBER(pEnt, mm.mrSurface)))
		gclCamera_Shake(0.125f, 0.2f, 0.0f, 0.0f, 1.0f);

	if (pSettings->eMode == kCameraMode_AutoTarget)
	{
		gclAutoTargetFunc(pCamera, pCameraView, pSettings, pEnt, fElapsedTime, fRealElapsedTime);
	}
	else if (pSettings->eMode == kCameraMode_ChaseCamera)
	{
		gclChaseCameraFunc(pCamera, pCameraView, fElapsedTime, fRealElapsedTime);
	}
	else
	{
		gclCamera_GetInput(pCamera, pCameraView, fRealElapsedTime);
	}

	pSettings->bIsInAimMode = entIsAiming(pEnt);

	if (g_CurrentScheme.bEnableShooterCamera)
	{
		if (pSettings->eMode != kCameraMode_ShooterCamera)
		{
			gclCamera_SetMode(kCameraMode_ShooterCamera, true);
		}
		if (pSettings->eMode == kCameraMode_ShooterCamera)
		{
			gclShooterCameraFunc(pCamera, pCameraView, fElapsedTime, fRealElapsedTime);
		}
	}
	else
	{
		if (pSettings->eMode == kCameraMode_ShooterCamera)
		{
			gclCamera_SetLastMode(true, true);
		}

		if (pSettings->bIsInAimMode && g_CurrentScheme.bUseZoomCamWithAimMode)
		{
			if (pSettings->eMode != kCameraMode_AimCamera)
			{
				gclCamera_UseAimCamera(true);
			}
			if (pSettings->eMode == kCameraMode_AimCamera)
			{
				((AimCameraSettings*)(pSettings->pModeSettings))->fTime = 0.0f;
			}
		}
		else if (pSettings->eMode == kCameraMode_AimCamera)
		{
			AimCameraSettings* pAimSettings = (AimCameraSettings*)pSettings->pModeSettings;
			if (pAimSettings->fTime >= 0.0f)
			{
				gclCamera_UseAimCamera(false);
			}
			pAimSettings->fTime += fRealElapsedTime;
		}
		if (pSettings->eMode == kCameraMode_AimCamera)
		{
			gclAimCameraFunc(pCamera, pCameraView, fElapsedTime, fRealElapsedTime);
		}
		if (pSettings->fGiganticPlayerHeight > FLT_EPSILON)
		{
			F32 fPlayerHeight = entGetHeight(pEnt);
			if (pSettings->eMode != kCameraMode_GiganticCamera)
			{
				if (pSettings->fGiganticPlayerHeight <= fPlayerHeight)
				{
					gclCamera_SetMode(kCameraMode_GiganticCamera, true);
				}
			}
			else if (pSettings->fGiganticPlayerHeight > fPlayerHeight)
			{
				gclCamera_SetLastMode(true, true);
			}
		}
	}

	if(pSettings->bIsInInspectMode && (entIsInCombat(pEnt) || !gclPlayerControl_IsAlwaysUsingMouseLookForced()))
	{
		gclCamera_TogglePlayerInspect();
	}

	if(pSettings->bIsInInspectMode)
	{
		ui_GenSetGlobalStateName("FullscreenUI", true);
	}

	// compute desired position before doing z distance changes
	if (pSettings->focusOverride.bEnabled)
	{
		gclCamera_UpdateFocusOverride(fElapsedTime, pCamera, pSettings);
	}
	else if (pSettings->pFocusSmoothingData)
	{
		CameraFocusSmoothingData* pFocusSmoothing = pSettings->pFocusSmoothingData;
		Vec3 vNewCameraCenter;

		int iNumHistoryNodes = eaSize(&pFocusSmoothing->eaPositionSmoothHistory);
		if (iNumHistoryNodes && distance3Squared(pFocusSmoothing->eaPositionSmoothHistory[iNumHistoryNodes-1]->vVal,pCamera->camcenter) > SQR(pFocusSmoothing->fAutoTeleportDist))
		{
			// the camera focus is assumed to have moved discontinuously
			eaClearStruct(&pFocusSmoothing->eaPositionSmoothHistory,parse_CameraSmoothNode);
		}
		gclCamera_GetSmoothVal(&pFocusSmoothing->eaPositionSmoothHistory,
								 pFocusSmoothing->fFilterMagnitude,
								 pFocusSmoothing->fFilterSampleTime,
								 fElapsedTime,
								 pCamera->camcenter,
								 vNewCameraCenter);

		if (distance3Squared(vNewCameraCenter,pCamera->camcenter) > SQR(pFocusSmoothing->fMaxLagDist))
		{
			// clamp to the max lag distance
			Vec3 vRealPosToLagPos;
			subVec3(vNewCameraCenter,pCamera->camcenter,vRealPosToLagPos);
  			normalVec3(vRealPosToLagPos);
			scaleVec3(vRealPosToLagPos,pFocusSmoothing->fMaxLagDist,vRealPosToLagPos);
			addVec3(pCamera->camcenter,vRealPosToLagPos,vNewCameraCenter);
		}

		copyVec3(vNewCameraCenter,pCamera->camcenter);
	}

	// if the player is trying to look up, adjust the camera so that the character doesn't block the screen
	if (pCamera->campyr[0] < 0.0f && SAFE_MEMBER3(pEnt, pChar, pattrBasic, fFlight) == 0.0f)
	{
		F32 camOffset;
		if (pSettings->pfDistancePresets[0])
		{
			pSettings->fCameraCenterOffset = (-pCamera->campyr[0]) * 6.0f * (pCamera->camdist/pSettings->pfDistancePresets[0]);
			camOffset = MAX(pCamera->camcenter[1] + pSettings->fCameraCenterOffset, pCamera->camcenter[1]);
			pCamera->camcenter[1] = camOffset;
		}

		//make sure we don't try and look up too far
		if ( pCamera->campyr[0] < pSettings->fMinPitch || pCamera->targetpyr[0] < pSettings->fMinPitch ) 
		{
			pCamera->campyr[0] = pSettings->fMinPitch;
			pCamera->targetpyr[0] = pSettings->fMinPitch;
		}
	}
	else if (pSettings->fMaxPitch && (pCamera->campyr[0] > pSettings->fMaxPitch || pCamera->targetpyr[0] > pSettings->fMaxPitch)) 
	{
		pCamera->campyr[0] = pSettings->fMaxPitch;
		pCamera->targetpyr[0] = pSettings->fMaxPitch;
	}

	gclCamera_Interp(pCamera, fRealElapsedTime);
	gclCamera_UpdateHeightMultiplier(pEnt, pCamera, fRealElapsedTime);
	gclCamera_GetShake(pCamera, postCamFXPYR, postCamFXOffset, fRealElapsedTime);

	gclCameraControllerUpdateProjectionFOV(pCamera, fElapsedTime, fRealElapsedTime);

	pSettings->fZoomMult = 1.0f;

	if (pSettings->bAdjustOffsetsByCapsuleSize &&
		pSettings->fDefaultCapsuleHeight > FLT_EPSILON)
	{
		F32 fHeight = entGetHeight(pEnt);
		F32 fWidth = entGetWidth(pEnt);
		MAX1F(fHeight, fWidth);
		if (!nearSameF32(fHeight, pSettings->fDefaultCapsuleHeight))
		{
			F32 fZoomMultWeight = pSettings->fZoomMultWeight;
			pSettings->fZoomMult = (fHeight / pSettings->fDefaultCapsuleHeight);
			
			if (fHeight > pSettings->fDefaultCapsuleHeight)
			{
				fZoomMultWeight = pSettings->fZoomMultWeightLargeEntity;
			}
			if (!nearSameF32(pSettings->fZoomMultWeight, 1.0f))
			{
				pSettings->fZoomMult = ((pSettings->fZoomMult - 1.0f) * fZoomMultWeight) + 1.0f;
			}
		}
	}
	//clamp the distance to closest allowed distance
	MAX1F(pSettings->fDistance, gclCamera_GetClosestDistance(pSettings));

	copyVec3(pCamera->camcenter, pCamera->camfocus);
	 
	if (pSettings->bFlightZoom)
		pCamera->camfocus[1] += gclCamera_GetFlightZoomHeightOffet(pCamera, fRealElapsedTime);
	else
		pCamera->camfocus[1] += gclCamera_GetPlayerHeight(pCamera);

	copyVec3(pCamera->camfocus, v3Temp);

	// apply the x-offset
	if (!pSettings->bUseNearOffset || pCamera->camdist > gclCamera_GetNearOffsetMaxRange(pSettings))
	{
		v3Temp[0] += -cos(postCamFXPYR[1]) * pSettings->fOffset;
		v3Temp[2] += sin(postCamFXPYR[1]) * pSettings->fOffset;
	}
	else
	{
		F32 fX, fZ;
		gclCamera_GetNearOffset(pCamera, pEnt, postCamFXPYR, &fX, &fZ);
		v3Temp[0] += fX;
		v3Temp[2] += fZ;
		setVec3(pSettings->vCurrentOffset, fX, 0.0f, fZ);
	}
	createMat3YPR(xCameraMatrix, postCamFXPYR);

	scaleVec3(xCameraMatrix[2], pCamera->camdist, xCameraMatrix[3]);
	addVec3(xCameraMatrix[3], v3Temp, xCameraMatrix[3]);

	if (pSettings->bFlightZoom)
	{
		Mat4 xTempMatrix;
		F32 fDistScale = -1, fOffsetLength = -1;

		//if using flight zoom, compute the campos to camcenter matrix
		if (pCamera->camdist > 0.1f)
		{
			Vec3 vOffsetDir;
			F32 s, c;
			subVec3(xCameraMatrix[3], pCamera->camcenter, vOffsetDir);
			fOffsetLength = normalVec3(vOffsetDir);

			c = CLAMPF32(dotVec3(xCameraMatrix[2], vOffsetDir), 0.0f, 1.0f);
			s = sin(-acos(c));

			fDistScale = fOffsetLength > 0.f ? pCamera->camdist / fOffsetLength : -1.f;

			//rotate Y and Z axes around X
			rotateVecAboutAxisEx(c, s, xCameraMatrix[0], xCameraMatrix[1], xTempMatrix[1]);
			rotateVecAboutAxisEx(c, s, xCameraMatrix[0], xCameraMatrix[2], xTempMatrix[2]);
			copyVec3(xCameraMatrix[0], xTempMatrix[0]);
			copyVec3(xCameraMatrix[3], xTempMatrix[3]);
		}
		else
		{
			copyMat4(xCameraMatrix, xTempMatrix);
		}

		gclCamera_FindProperZDistance(pCamera, xTempMatrix, fRealElapsedTime, fDistScale, -1);

		v3Temp[1] = pCamera->camcenter[1];
		v3Temp[1] += gclCamera_GetFlightZoomHeightOffet(pCamera, 0.0f);
	}
	else if (pSettings->fOffset > 0.01f 
		||	(pSettings->bUseNearOffset && pCamera->camdist <= gclCamera_GetNearOffsetMaxRange(pSettings)))
	{
		Mat4 xTempMatrix;
		F32 fX, fZ, fDistScale = -1;

		if (pCamera->camdist > 0.1f)
		{
			Vec3 vOffsetDir, vCenter;
			F32 fOffsetLength;
			setVec3(vCenter, pCamera->camcenter[0], v3Temp[1], pCamera->camcenter[2]);
			subVec3(xCameraMatrix[3], vCenter, vOffsetDir);
			fOffsetLength = normalVec3(vOffsetDir);
			orientMat3(xTempMatrix, vOffsetDir);
			copyVec3(xCameraMatrix[3], xTempMatrix[3]);

			fDistScale = fOffsetLength > 0.f ? pCamera->camdist / fOffsetLength : -1.f;
		}
		else
		{
			copyMat4(xCameraMatrix, xTempMatrix);
		}

		gclCamera_FindProperZDistance(pCamera, xTempMatrix, fRealElapsedTime, fDistScale, -1);

		gclCamera_GetNearOffset(pCamera, pEnt, postCamFXPYR, &fX, &fZ);
		copyVec3XZ(pCamera->camcenter, v3Temp);
		v3Temp[0] += fX;
		v3Temp[2] += fZ;
		setVec3(pSettings->vCurrentOffset, fX, 0.0f, fZ);
	}
	else
	{
		gclCamera_FindProperZDistance(pCamera, xCameraMatrix, fRealElapsedTime, -1, -1);
	}

	// Redo distance
	scaleVec3(xCameraMatrix[2],pCamera->camdist,xCameraMatrix[3]);
	addVec3(xCameraMatrix[3], v3Temp, xCameraMatrix[3]);

	// Apply fx offset for panning
	{
		Vec3 vTemp;
		mulVecMat3(postCamFXOffset, xCameraMatrix, vTemp);
		addVec3(xCameraMatrix[3], vTemp, xCameraMatrix[3]);
	}

	// FX camera matrix override.
	if(pSettings && pSettings->bUseFXCameraMatrixOverride) {
		
		Quat qFXOrientation;
		Quat qCamOrientation;
		Quat qOut;
		Mat4 xOutMat;

		// Interpolate between the normal camera control and the FX
		// camera control based on the camera influence specified in
		// the FX.
		mat3ToQuat(pSettings->xFXCameraMatrixOverride, qFXOrientation);
		mat3ToQuat(xCameraMatrix, qCamOrientation);		
		quatInterp(
			pSettings->fFXCameraInfluence,
			qCamOrientation,
			qFXOrientation,
			qOut);
		quatToMat(qOut, xOutMat);

		interpVec3(
			pSettings->fFXCameraInfluence,
			xCameraMatrix[3],
			pSettings->xFXCameraMatrixOverride[3],
			xOutMat[3]);

		copyMat4(xOutMat, xCameraMatrix);
	}

	globMovementLogCamera("pCamera.game", xCameraMatrix);
	globMovementLog("[gcl] Setting pCamera matrix at pos (%f, %f, %f)",
					vecParamsXYZ(xCameraMatrix[3]));

	if(pCameraView)
	{
		frustumSetCameraMatrix(&pCameraView->new_frustum, xCameraMatrix);
	}

	gclCamera_DoCollisionFade(pEnt, pCamera, xCameraMatrix[3]);

	gclCamera_DoEntityCollisionFade(pEnt, xCameraMatrix[3]);

	exprCameraSuppressForcedMouseLockThisFrame(0);

	PERFINFO_AUTO_STOP();
}

void gclCutsceneCamFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsed, F32 fRealElapsed)
{
	Vec3 up;
	F32 fUseElapseTime;

	setVec3(up, 0, 1, 0);

	fUseElapseTime = ( wlTimeGetStepScale()==1.0f ? fRealElapsed : fElapsed );

	if (gclCutsceneIsSet())
	{
		if (gclCutsceneAnimListsAreLoaded(fRealElapsed))
		{
			// This will populate the camera center and camera PYR if there is an active cutscene
			if(gclGetCutsceneCameraPosPyr(fUseElapseTime, pCamera->camcenter, pCamera->campyr, pCameraView->sky_data))
			{
				Mat4 xCameraMatrix;
				Vec3 postCamPYR;
				Vec3 postCamOffset;

				// Let the camera apply any effects to the camera
				gclCamera_GetShake(pCamera, postCamPYR, postCamOffset, fUseElapseTime);

				// Create a camera matrix from camcenter and campyr
				createMat3YPR(xCameraMatrix, postCamPYR);
				{
					Vec3 vTemp;
					mulVecMat3(postCamOffset, xCameraMatrix, vTemp);
					addVec3(pCamera->camcenter, vTemp, xCameraMatrix[3]);
				}

				copyVec3(pCamera->camcenter, pCamera->camfocus);

				frustumSetCameraMatrix(&pCameraView->new_frustum, xCameraMatrix);

			} else {
				gclCutsceneEndOnClient(true);
			}
		}
	} else {
		gclCutsceneEndOnClient(true);
	}
	
}

//////////////////////////////////////////////////////////////////////////
// Stuff relating to prelogin/login camera scenes.

static GfxCameraController s_LoginCameraController;
static bool s_LoginCameraUnfixOffset;

GfxCameraController *gclLoginGetCameraController(void)
{
	return &s_LoginCameraController;
}

void gclLoginCamFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 elapsed, F32 real_elapsed)
{
	Mat4 xCameraMatrix;
	Vec3 camlocaloffset;

	if(gbNoGraphics)
	{
		return;
	}

 	createMat3YPR(xCameraMatrix, pCamera->campyr);

	// Redo distance
	if (!s_LoginCameraUnfixOffset)
	{
		pCamera->camdist = 10.0;
	}

	setVec3(camlocaloffset, 0, 0, pCamera->camdist );
	addVec3(camlocaloffset, pCamera->centeroffset, camlocaloffset);
	mulVecMat3(camlocaloffset, xCameraMatrix, xCameraMatrix[3]);
	addVec3(xCameraMatrix[3], pCamera->camcenter, xCameraMatrix[3]);
	copyVec3(pCamera->camcenter, pCamera->camfocus);

	frustumSetCameraMatrix(&pCameraView->new_frustum, xCameraMatrix);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CameraIsInMode");
bool gclCameraExpr_IsInMode(S32 eCameraMode)
{
	return gclCamera_IsInMode(eCameraMode);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CameraIsUsingTargetLock");
bool gclCameraExpr_IsUsingTargetLock(void)
{
	return gclCamera_IsInMode(kCameraMode_AutoTarget);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CameraSetMouseLock");
void gclCameraExpr_SetMouseLock(bool bLock)
{
	gclPlayerControl_UpdateMouseInput(false, bLock);
	gclCamera_OnMouseLook(bLock);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CameraIsMouseLookForced");
bool gclCameraExpr_IsMouseLookForced()
{
	return gclPlayerControl_IsAlwaysUsingMouseLookForced();
}
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CameraIsUsingMouseLook");
bool gclCameraExpr_IsUsingMouseLook(void)
{
	return gclCamera_IsMouseLooking(gfxGetActiveCameraController());
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("CameraIsInspectMode");
bool gclCameraExpr_IsInInspectMode()
{
	return gclCamera_IsInInspectMode();
}

// Set the scene override name for the camera during the login process. Give a name
// of "" to reset the scene to the default.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LoginCameraSetSky");
void gclCameraExpr_LoginCameraSetSky(ExprContext *pContext, const char *pchSky)
{
	gfxCameraControllerSetSkyOverride(&s_LoginCameraController, pchSky, exprContextGetBlameFile(pContext));
}

// Set the time override for the login progress. Give -1 to reset the time to the default.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LoginCameraSetTime");
void gclCameraExpr_LoginCameraSetTime(ExprContext *pContext, F32 fTime)
{
	s_LoginCameraController.override_time = fTime > 0;
	s_LoginCameraController.time_override = fTime;
}

// Set the camera center offset
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LoginCameraSetOffset");
void gclCameraExpr_LoginCameraSetOffset(F32 fX, F32 fY, F32 fZ)
{
	setVec3(s_LoginCameraController.centeroffset, fX, fY, fZ);
}

// Set the camera center offset to the defaults
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LoginCameraSetOffsetDefault");
void gclCameraExpr_LoginCameraSetOffsetDefault(void)
{
	setVec3(s_LoginCameraController.centeroffset, 2, 3.65, 0);
}

// Set the camera center offset
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LoginCameraSetDistance");
void gclCameraExpr_LoginCameraSetDistance(F32 fDist, bool bUnfixOffset)
{
	s_LoginCameraUnfixOffset = bUnfixOffset;
	s_LoginCameraController.camdist = fDist;
}

// Set the camera center offset to the defaults
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("LoginCameraSetDistanceDefault");
void gclCameraExpr_LoginCameraSetDistanceDefault(void)
{
	s_LoginCameraUnfixOffset = false;
	s_LoginCameraController.camdist = 10.0;
}

AUTO_RUN;
void gclCamera_Init(void)
{
	gfxInitCameraController(&s_LoginCameraController, gclLoginCamFunc, NULL);
	s_LoginCameraController.do_shadow_scaling = 1;
	gclCameraExpr_LoginCameraSetDistanceDefault();
	gclCameraExpr_LoginCameraSetOffsetDefault();

	ui_GenInitStaticDefineVars(CameraModeEnum, "CameraMode_");
}

#include "gclCamera_h_ast.c"
