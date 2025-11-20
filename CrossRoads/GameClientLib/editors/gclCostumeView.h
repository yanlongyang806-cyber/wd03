//
// CostumeView.h
//

#pragma once
GCC_SYSTEM

#include "dynDraw.h"
#include "dynSkeleton.h"
#include "CostumeCommonTailor.h"
#include "WeaponStance.h"
#include "wlCostume.h"
#include "gclCostumeUIState.h"


typedef struct GfxCameraController GfxCameraController;
typedef struct PCBoneDef PCBoneDef;
typedef struct PlayerCostume PlayerCostume;
typedef struct SceneInfo SceneInfo;
typedef struct CBox CBox;
typedef struct WeaponFXRef WeaponFXRef;
typedef struct CharacterClass CharacterClass;
extern CostumeEditState g_CostumeEditState;

typedef struct CostumeViewCostume {
	const char *pcName;
	REF_TO(WLCostume)	hWLCostume;

	DynSkeleton		*pSkel;
	DynDrawSkeleton *pDrawSkel;
	DynNode			*pCamOffset;
	DynFxManager	*pFxManager;
	DynBitFieldGroup costumeBFG;

	WorldRegion *pFxRegion;

	REF_TO(TailorWeaponStance) hWeaponStance;
	DynParamBlock *pWeaponFxParams;
	WeaponFXRef **eaWeaponFx;
	const char *pcBits;

	const char *pcAnimStanceWords;
	const char *pcLastAnimStanceWords;
	const char *pcAnimKeyword;
	const char *pcAnimMove;
	const char *pcLastAnimMove;

	U32 iModelMemory, iMaterialMemory;

	bool bPositionInCameraSpace;
	Vec3 v3SkelPos;
	Quat qSkelRot;

	bool bReset;
	bool bResetAnimation;
	bool bResetFX;
	bool bResetFXManager;
	bool bIgnoreSkelFX;
	bool bNeverKeepSequencers;
	bool bNeedsResetToDefault;
} CostumeViewCostume;

// Costume graphics structure
typedef struct CostumeViewGraphics {
	CostumeViewCostume costume;

	CostumeViewCostume **eaExtraCostumes;

	// If override is enabled, the time/sky override is applied each draw
	// If not enabled, the camera is left alone
	bool bOverrideTime;
	bool bOverrideSky;
	F32 fTime;           // Zero means no override
	char *pcSkyOverride; // NULL means to use the 

	bool bOverrideForceEditorRegion; // Force editor region for lighting, instead of getting the region based on the position.
} CostumeViewGraphics;


// Make sure graphics global data is initialized
void costumeView_InitGraphics(void);

// update the camera
void costumeView_UpdateCamera(CostumeViewGraphics* pGraphics, 
							  const CBox* pBox, 
							  F32 fCostumeHeight, 
							  F32 fHeightMod,
							  F32 fDepthMod, 
							  bool bZoom, 
							  F32 fZoom, 
							  F32 fZoomMaxOverride,
							  bool bComputeHeightOffset, 
							  F32 *fZoomSmoothing);
// A new way to update the camera
void costumeView_CalculateCamera(CostumeViewGraphics* pGraphics,
								 const CBox *pBox,
								 F32 fOriginX,
								 F32 fOriginY,
								 F32 fCostumeWidth,
								 F32 fCostumeHeight,
								 F32 fWidthMod,
								 F32 fHeightMod,
								 F32 fDepthMod,
								 F32 fZoom);
//reset the fit parameters
void costumeView_ResetCamera(void);

// Get/Set the camera
void costumeView_SetCamera(GfxCameraController *pCamera);
GfxCameraController *costumeView_GetCamera(void);

// Get/Set the root node
DynNode *costumeView_GetRootNode(void);

// -- Per-Frame Functions --

// Called per-frame for the costume to be drawn
void costumeView_Draw(CostumeViewGraphics *pGraphics);
void costumeView_DrawViewCostume(CostumeViewCostume *pViewCostume);

// Called during the per-frame drawing of ghosts
void costumeView_DrawGhosts(CostumeViewGraphics *pGraphics);
void costumeView_DrawGhostsViewCostume(CostumeViewCostume *pViewCostume, bool bForceEditorRegion);

// Called per-frame to draw lines that help show scale
void costumeView_DrawHeightRuler(F32 fHeight);

// Called when a costume instance's graphics need reinitializing
void costumeView_ReinitDrawSkeleton(CostumeViewCostume* pGraphics);

// -- Costume Management Functions --

// Create the pGraphics structure
CostumeViewGraphics *costumeView_CreateGraphics(void);
CostumeViewCostume *costumeView_CreateViewCostume(CostumeViewGraphics *pGraphics);

// Regenerate the costume and initialize the pGraphics structure
#define costumeView_RegenCostume(pGraphics, pCostume, pMood) costumeView_RegenCostumeEx(pGraphics, pCostume, NULL, pMood, NULL)
void costumeView_RegenCostumeEx(CostumeViewGraphics *pGraphics, PlayerCostume *pCostume, const PCSlotType *pSlotType, PCMood *pMood, CharacterClass* pClass);
void costumeView_RegenViewCostume(CostumeViewCostume *pViewCostume, PlayerCostume *pCostume, const PCSlotType *pSlotType, PCMood *pMood, CharacterClass* pClass);

// Temporarily stop FX until next regen costume
void costumeView_StopFx(CostumeViewGraphics *pGraphics);
void costumeView_StopViewCostumeFx(CostumeViewCostume *pViewCostume);

// Close the pGraphics structure and release its resources
void costumeView_FreeGraphics(CostumeViewGraphics *pGraphics);
void costumeView_FreeViewCostume(CostumeViewCostume *pViewCostume, bool bFree);

void costumeView_SetPos(CostumeViewGraphics *pGraphics, const Vec3 v3Pos);
void costumeView_SetRot(CostumeViewGraphics *pGraphics, const Quat qRot);
void costumeView_SetPosRot(CostumeViewGraphics *pGraphics, const Vec3 v3Pos, const Quat qRot);
void costumeView_SetViewCostumePos(CostumeViewCostume *pViewCostume, const Vec3 v3Pos);
void costumeView_SetViewCostumeRot(CostumeViewCostume *pViewCostume, const Quat qRot);
void costumeView_SetViewCostumePosRot(CostumeViewCostume *pViewCostume, const Vec3 v3Pos, const Quat qRot);

PCBoneDef *costumeView_GetSelectedBone(CostumeViewGraphics *pGraphics, NOCONST(PlayerCostume) *pCostume);
