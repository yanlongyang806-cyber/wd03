/***************************************************************************
*     Copyright (c) 2006-2007, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
**************************************************************************/

typedef struct CBox CBox;
typedef struct CostumeViewGraphics CostumeViewGraphics;
typedef struct GfxCameraController GfxCameraController;
typedef struct GfxCameraView GfxCameraView;
typedef struct PlayerCostume PlayerCostume;
typedef struct UIGen UIGen;

void costumeCameraUI_FitCameraToBox(SA_PARAM_NN_VALID CBox *pBox, bool bScaleToHeight, bool bZoom, bool bAllowMouseWheel, PlayerCostume *pPlayerCostume, PCStanceInfo *pStance, CostumeViewGraphics *pCameraView);
GfxCameraController *costumeCameraUI_GetCamera(void);
void costumeCameraUI_CameraOncePerFrame(CostumeViewGraphics *pCameraView, GfxCameraView *pGFXCameraView, bool bCreatorActive);
void costumeCameraUI_DrawGhosts(void);
void costumeCameraUI_CostumeCreatorCamFunc(GfxCameraController *camera, GfxCameraView *camera_view, F32 elapsed, F32 real_elapsed);
void costumeCameraUI_CreateWorld(GfxCameraView *pCamView);
int costumeCameraUI_IsShowingCostumeItems(void);
void costumeCameraUI_FitCameraToGenInternal(SA_PARAM_NN_VALID UIGen *pGen, bool bScaleToHeight, bool bZoom, bool bAllowMouseWheel, PlayerCostume *pPlayerCostume, PCStanceInfo *pStance, CostumeViewGraphics *pCameraView);
void costumeCameraUI_FitCostumeToGenInternal(SA_PARAM_NN_VALID UIGen *pGen, bool bScaleToHeight, F32 fOriginX, F32 fOriginY, F32 fZoom, PlayerCostume *pPlayerCostume, const char *pcSelectedBone, const char *pcCenterBone, bool bBothMode, PCStanceInfo *pStance, CostumeViewGraphics *pCameraView);
void costumeCameraUI_SetHeightMod(F32 fValue);
void costumeCameraUI_SetMouseRotate(bool bRotate);
void costumeCameraUI_SetMouseRotate3D(bool bRotate);
void costumeCameraUI_SetZoomMult(float fMult);
void costumeCameraUI_SetRotMult(float fMult);
void costumeCamearaUI_ResetCameraFit(void);
void costumeCameraUI_SetFxRegion(WorldRegion *pFxRegion);
void costumeCameraUI_ClearBackgroundCostumes(void);





