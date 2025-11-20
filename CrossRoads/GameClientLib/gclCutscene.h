
#ifndef GCLCUTSCENE_H
#define GCLCUTSCENE_H
GCC_SYSTEM

#include "textparser.h"

typedef struct CutsceneDef CutsceneDef;
typedef struct CutscenePathList CutscenePathList;
typedef struct CutscenePath CutscenePath;
typedef struct GfxSkyData GfxSkyData;
typedef struct CutsceneWorldVars CutsceneWorldVars;
typedef struct Entity Entity;

AUTO_STRUCT;
typedef struct ClientCutscene
{
	CutsceneDef* pDef;
	F32 elapsedTime;	NO_AST
	F32 runningTime;	NO_AST
	CutsceneWorldVars* pWorldVars;
		//	CutsceneLoadState loadState;
} ClientCutscene;

AUTO_STRUCT;
typedef struct TransitionTextDef
{
	char* pchFont;		AST(NAME(Font) DEFAULT("Game_HUD"))
	F32 fPercentX;		AST(NAME(PercentX) DEFAULT(0.5f))
	F32 fPercentY;		AST(NAME(PercentY) DEFAULT(1.0f))
	F32 fScale;			AST(NAME(Scale) DEFAULT(1.0f))
	F32 fStartTime;		AST(NAME(StartTime) DEFAULT(1.0f))
	F32 fEndTime;		AST(NAME(EndTime) DEFAULT(6.0f))
	F32 fFadeTime;		AST(NAME(FadeTime) DEFAULT(0.5f))
} TransitionTextDef;

bool gclCutsceneIsSet(void);
void gclCutsceneStart(Vec3 cameraPos, Vec3 cameraPyr, F32 fStartTime);
void gclCutsceneEndOnClient(bool bTerminateNow);

// Used to hold off on playing a cutscene until the AIAnimLists have arrived
bool gclCutsceneAnimListsAreLoaded(F32 timestep);

// Camera function
bool gclGetCutsceneCameraPosPyr(F32 timestep, Vec3 cameraPos /*out*/, Vec3 cameraPYR /*out*/, GfxSkyData *sky_data);
bool gclGetCutsceneCameraPathListPosPyr(CutsceneDef *pDef, F32 elapsedTime, Vec3 cameraPos /*out*/, Vec3 cameraPYR /*out*/, GfxSkyData *sky_data, bool reset, F32 timestep);
bool gclGetCutsceneCameraBasicPosPyr(CutsceneDef *pDef, Vec3 cameraPos /*out*/, Vec3 cameraPYR /*out*/);

//Track List
void gclCutsceneLoadSplines(CutscenePathList* pTrackList);
void gclCutscenePathLoadSplines(CutscenePath* pTrack);

typedef void (*gclCutsceneFinishedCB)(UserData);
void gclCutsceneSetFinishedCB(gclCutsceneFinishedCB func, UserData);

void gclCutsceneCGTParentMat(CutsceneDef *pDef, void *pCGT_In, Mat4 parentMat);
Entity* gclCutsceneGetCutsceneEntByName(CutsceneDef *pDef, const char *pcName);

void gclCutsceneGetScreenSize(S32 *iWindowWidth, S32 *iWindowHeight, S32 *iLetterBox, F32 elapsedTime);

void gclCleanDynamicData(SA_PARAM_OP_VALID CutsceneDef *pDef);
void gclCleanDynamicDataEx(SA_PARAM_OP_VALID CutsceneDef *pDef, SA_PARAM_OP_VALID CutsceneDef *pNextDef);

// Attempt to load all the time 0 information, return true on success.
bool gclCutscenePreLoad(CutsceneDef* pCutscene);

void gclCutsceneStartEx(Vec3 cameraPos, Vec3 cameraPyr, F32 fStartTime, ClientCutscene *pCutscene);

#endif // GCLCUTSCENE_H