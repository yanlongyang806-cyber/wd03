/***************************************************************************
*     Copyright (c) 2011, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "ReferenceSystem.h"

typedef struct CutsceneDef CutsceneDef;
typedef struct GfxCameraController GfxCameraController;
typedef struct GfxCameraView GfxCameraView;
typedef struct ItemDef ItemDef;
typedef struct Item Item;

AUTO_STRUCT;
typedef struct ChoosableItem
{
	REF_TO(ItemDef) hItemDef;
	Item* pItem; AST(UNOWNED)
	S32 iBagIdx;
	S32 iNumPicks;
	bool bSelected;
} ChoosableItem;

// Clean up the current cutscene
void contactui_CleanUpCurrentCutscene(CutsceneDef *pNextCutScene);

// Resets the camera back to the game camera
void contactui_ResetToGameCamera(void);

// Contact cutscene camera function
void contactCutSceneCameraFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsed, F32 fRealElapsed);