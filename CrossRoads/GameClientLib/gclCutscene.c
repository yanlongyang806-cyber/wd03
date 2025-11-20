#include "fileutil.h"
#include "FolderCache.h"
#include "inputData.h"
#include "inputKeyBind.h"
#include "GraphicsLib.h"
#include "gfxPrimitive.h"
#include "GfxSprite.h"
#include "GfxTexAtlas.h"
#include "GfxFont.h"
#include "GfxSpriteText.h"
#include "GfxSprite.h"
#include "Player.h"
#include "Quat.h"
#include "mission_common.h"
#include "gclUIGenMapExpr.h"
#include "smf_render.h"
#include "sm_parser.h"
#include "contact_common.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#include "EditorManager.h"
#include "soundLib.h"
#include "wlCurve.h"
#include "wlCostume.h"

#include "AnimList_Common.h"
#include "GameClientLib.h"
#include "Character.h"
#include "CharacterAttribsMinimal.h"
#include "AutoGen/Character_h_ast.h"
#include "cutscene_common.h"
#include "gclEntity.h"
#include "gclCamera.h"
#include "gclCommandParse.h"
#include "gclDemo.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "CostumeCommonLoad.h"
#include "DoorTransitionCommon.h"
#include "EntityClient.h"
#include "EntityIterator.h"
#include "EntityMovementManager.h"
#include "EntityMovementDefault.h"
#include "EntityMovementFX.h"
#include "EntityMovementInteraction.h"
#include "EntityLib.h"
#include "EntitySavedData.h"
#include "AutoGen/Entity_h_ast.h"
#include "GameStringFormat.h"
#include "dynFxInterface.h"
#include "dynFxManager.h"
#include "dynSkeleton.h"
#include "StringCache.h"
#include "gclBaseStates.h"
#include "GlobalStateMachine.h"
#include "StringUtil.h"
#include "chat/gclChatLog.h"
#include "contactui_eval.h"
#include "GameAccountDataCommon.h"
#include "PowerAnimFX.h"
#include "PowersMovement.h"
#include "CostumeCommonTailor.h"
#include "Guild.h"
#include "mapstate_common.h"
#include "ChatCommonStructs.h"
#include "TeamUpCommon.h"
#include "inputMouse.h"

#include "gclCutscene.h"
#include "../StaticWorld/ZoneMap.h"

#include "UIGen.h"

#include "Autogen/ChatData_h_ast.h"
#include "Autogen/cutscene_common_h_ast.h"
#include "Autogen/costumecommon_h_ast.h"
#include "Autogen/gclCutscene_h_ast.c"
#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

bool g_CutsceneDrawDebugInfo = false;
AUTO_CMD_INT(g_CutsceneDrawDebugInfo, CutsceneDrawDebugInfo) ACMD_CMDLINE;

int g_iCutsceneMapTransferAddTime = 5.0f;
AUTO_CMD_INT(g_iCutsceneMapTransferAddTime, CutsceneMapTransferAddTime) ACMD_CMDLINE;


// ----------------------------------------------------------------------------------
// Cutscene callbacks
// ----------------------------------------------------------------------------------

ClientCutscene *g_ClientCutscene = NULL;

#define CutsceneErrorf(pchFormat, ...) if(g_ClientCutscene && g_ClientCutscene->pDef && g_ClientCutscene->pDef->filename) { ErrorFilenamef(g_ClientCutscene->pDef->filename, pchFormat, __VA_ARGS__); }

static F32 prevElapsedTime;
static int prevPathIdx;
static int prevPosIdx;
static Vec3 prevPos;
static int prevTargIdx;
static Vec3 prevPyr;
static bool prevPosEaseOut;
static bool prevLookAtEaseOut;
static bool firstCutsceneFrame = false;
static SMFBlock *s_pSubtitleBlock = NULL;
static SMFBlock *s_pTransTextBlock = NULL;
static TextAttribs *s_pTransTextAttribs = NULL;
static TransitionTextDef s_TransitionTextDef = { 0 };
static F32 s_fLetterboxWindowHeight = 0.075;
static S32 s_iLetterboxMinHeight = 50;
static F32 s_fCutsceneTextFadeTime = 0.3;
static bool s_bCutsceneShowTime = false;
static F32 s_fCutsceneAnimLoadWaitTime = 0.0f;
static bool s_bCutsceneAnimsLoaded = false;
static F32 s_fCutscenePauseThenEndTime = 0.0f;
static DoorTransitionType s_eDoorTransitionType = kDoorTransitionType_Unspecified;
static const char **s_ppPlayedSounds = NULL;
static const char **s_ppNeedToStopSounds = NULL;
static U32 *s_pRunningFX = NULL;
static ClientOnlyEntity **s_ppCOEnts = NULL;
static GfxSkyData *pOldSkyData;
AUTO_CMD_FLOAT(s_fLetterboxWindowHeight, CutsceneLetterboxWindowHeight) ACMD_HIDE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(s_iLetterboxMinHeight, CutsceneLetterboxMinHeight) ACMD_HIDE ACMD_ACCESSLEVEL(0);
AUTO_CMD_FLOAT(s_fCutsceneTextFadeTime, CutsceneTextFadeTime) ACMD_HIDE ACMD_ACCESSLEVEL(0);
AUTO_CMD_INT(s_bCutsceneShowTime, CutsceneShowTime) ACMD_HIDE;

typedef enum CutsceneRunFunctionAction
{
	CRFA_Start = 0,
	CRFA_Mid,
	CRFA_End,
} CutsceneRunFunctionAction;

typedef struct CutsceneFrameState
{
	CutsceneDef *pDef;
	GfxSkyData *pSkyData;
	GfxCameraController *pCamera;
	Vec3 vCamPos;
	Vec3 vLookPos;
	Vec3 vFinalPos;
	Vec3 vFinalPyr;
} CutsceneFrameState;

static bool gclCutsceneBasicInitStartPos(CutsceneDef *pDef);
static void gclCutsceneInitSubtitleBlock(CutsceneDef *pDef, SMFBlock **pSubtitleBlock, const char *pTranslatedMessage, bool bCenter);
static bool gclCutsceneParentEntityExistsInDef(SA_PARAM_NN_VALID CutsceneDef *pDef, EntityRef erParentEnt, CutsceneEntityList **pListFound);
static void gclCutsceneSetAllPlayersAlpha(F32 fAlpha, bool bNoInterpAlpha);

static gclCutsceneFinishedCB s_finishedCB = NULL;
static UserData s_finishedUserData = NULL;
void gclCutsceneSetFinishedCB(gclCutsceneFinishedCB func, UserData pUserData)
{
	s_finishedCB = func;
	s_finishedUserData = pUserData;
}

static void gclCutsceneSetFOV(GfxCameraController *pCamera, F32 fov)
{
	pCamera->default_projection_fov = fov;
	pCamera->projection_fov			= fov;
	pCamera->target_projection_fov	= fov;
}

static const char *gclCutsceneFindVariableString(CutsceneFrameState *pState, const char *varName)
{
	WorldVariable* pMapVar = (varName && varName[0]) ? mapState_GetPublicVarByName(mapState_FromEnt(entActivePlayerPtr()), varName) : NULL;
	if(!pMapVar && varName && varName[0] && g_ClientCutscene && g_ClientCutscene->pWorldVars)
	{
		//Look for the var in the updated list
		int i;
		for(i = 0; i < eaSize(&g_ClientCutscene->pWorldVars->eaWorldVars); i++)
		{
			if(strcmp(g_ClientCutscene->pWorldVars->eaWorldVars[i]->pcName, varName) == 0)
			{
				pMapVar = g_ClientCutscene->pWorldVars->eaWorldVars[i];
				break;
			}
		}
		if(!pMapVar)
			ErrorFilenamef(pState->pDef->filename, "Cutscene is using a world variable that does not exist: %s", varName);
	}

	if(pMapVar && pMapVar->eType != WVAR_STRING)
		ErrorFilenamef(pState->pDef->filename, "Cutscene is using a String world variable that is not of type String: %s", varName);

	if(pMapVar && pMapVar->eType == WVAR_STRING)
		return pMapVar->pcStringVal;

	return NULL;
}

static DisplayMessage *gclCutsceneFindVariableMessage(CutsceneFrameState *pState, const char *varName)
{
	WorldVariable* pMapVar = (varName && varName[0]) ? mapState_GetPublicVarByName(mapState_FromEnt(entActivePlayerPtr()), varName) : NULL;
	if(!pMapVar && varName && varName[0] && g_ClientCutscene && g_ClientCutscene->pWorldVars)
	{
		//Look for the var in the updated list
		int i;
		for(i = 0; i < eaSize(&g_ClientCutscene->pWorldVars->eaWorldVars); i++)
		{
			if(strcmp(g_ClientCutscene->pWorldVars->eaWorldVars[i]->pcName, varName) == 0)
			{
				pMapVar = g_ClientCutscene->pWorldVars->eaWorldVars[i];
				break;
			}
		}
		if(!pMapVar)
			ErrorFilenamef(pState->pDef->filename, "Cutscene is using a world variable that does not exist: %s", varName);
	}

	if(pMapVar && pMapVar->eType != WVAR_MESSAGE)
		ErrorFilenamef(pState->pDef->filename, "Cutscene is using a Message world variable that is not of type Message: %s", varName);

	if(pMapVar && pMapVar->eType == WVAR_MESSAGE)
		return &pMapVar->messageVal;

	return NULL;
}

static AtlasTex *gclCutsceneLoadTexture(CutsceneFrameState *pState, CutsceneTextureList *pList)
{
	AtlasTex *pTexture = NULL;
	if(pList)
	{
		const char *textureNameFromVariable = gclCutsceneFindVariableString(pState, pList->pcTextureVariable);
		if(textureNameFromVariable)
			pTexture = atlasLoadTexture(textureNameFromVariable);

		if(!pTexture && pList->pcTextureName && pList->pcTextureName[0])
			pTexture = atlasLoadTexture(pList->pcTextureName);
	}
	return pTexture;
}

static const char *gclCutsceneGetSoundPath(CutsceneFrameState *pState, CutsceneSoundPoint *pPoint)
{
	const char *pSoundPath = NULL;
	if(pPoint)
	{
		pSoundPath = gclCutsceneFindVariableString(pState, pPoint->pcSoundVariable);

		if(!pSoundPath && pPoint->pSoundPath && pPoint->pSoundPath[0])
			pSoundPath = pPoint->pSoundPath;
	}
	return pSoundPath;
}

// This helper will not always work in production because the message may not download from the GameServer in time for it to be sent to the UIGen.
// It always works in edit mode, though.
static DisplayMessage *gclCutsceneGetDisplayMessageForUIGen(CutsceneFrameState *pState, CutsceneUIGenPoint *pPoint)
{
	DisplayMessage *pDisplayMessage = NULL;
	if(pPoint)
	{
		pDisplayMessage = gclCutsceneFindVariableMessage(pState, pPoint->pcMessageValueVariable);

		if(!pDisplayMessage)
			pDisplayMessage = &pPoint->messageValue;
	}
	return pDisplayMessage;
}

// This helper will work in production and edit mode because in production the translated message is sent from the GameServer when the cutscene is launched.
static const char *gclCutsceneGetTranslatedMessageForUIGen(CutsceneFrameState *pState, CutsceneUIGenPoint *pPoint)
{
	if(pPoint->pcTranslatedMessage && pPoint->pcTranslatedMessage[0])
		return pPoint->pcTranslatedMessage;
	else
	{
		DisplayMessage *displayMessage = gclCutsceneGetDisplayMessageForUIGen(pState, pPoint);
		if(displayMessage)
			return TranslateDisplayMessageOrEditCopy(*displayMessage);
	}
	return "";
}

// This helper will not always work in production because the message may not download from the GameServer in time for it to be sent to the UIGen.
// It always works in edit mode, though.
static DisplayMessage *gclCutsceneGetDisplayMessageForSubtitle(CutsceneFrameState *pState, CutsceneSubtitlePoint *pPoint)
{
	DisplayMessage *pDisplayMessage = NULL;
	if(pPoint)
	{
		pDisplayMessage = gclCutsceneFindVariableMessage(pState, pPoint->pcSubtitleVariable);

		if(!pDisplayMessage)
			pDisplayMessage = &pPoint->displaySubtitle;
	}
	return pDisplayMessage;
}

// This helper will work in production and edit mode because in production the translated message is sent from the GameServer when the cutscene is launched.
static const char *gclCutsceneGetTranslatedMessageForSubtitle(CutsceneFrameState *pState, CutsceneSubtitlePoint *pPoint)
{
	if(pPoint->pcTranslatedSubtitle && pPoint->pcTranslatedSubtitle[0])
		return pPoint->pcTranslatedSubtitle;
	else
	{
		DisplayMessage *displayMessage = gclCutsceneGetDisplayMessageForSubtitle(pState, pPoint);
		if(displayMessage)
			return TranslateDisplayMessageOrEditCopy(*displayMessage);
	}
	return "";
}

static void gclTransitionTextDefLoad(const char *pchPath, S32 iWhen)
{
	loadstart_printf("Loading transition text def..");

	StructDeInit( parse_TransitionTextDef, &s_TransitionTextDef );

	if (pchPath)
	{
		fileWaitForExclusiveAccess(pchPath);
		errorLogFileIsBeingReloaded(pchPath);
	}

	ParserLoadFiles("ui", "TransitionText.def", "TransitionText.bin", PARSER_OPTIONALFLAG, parse_TransitionTextDef, &s_TransitionTextDef);

	loadend_printf(" Done. ");
}

AUTO_STARTUP(CutscenesClient);
void gclCutsceneLoad(void)
{
	if(!gbNoGraphics)
	{
		gclTransitionTextDefLoad(NULL, 0);
		FolderCacheSetCallback(FOLDER_CACHE_CALLBACK_UPDATE, "ui/TransitionText.def", gclTransitionTextDefLoad);
	}
}

static void CutsceneStopAllSounds()
{
	int i;
	for ( i=0; i < eaSize(&s_ppNeedToStopSounds); i++ ) {
		sndStopOneShot(s_ppNeedToStopSounds[i]);
	}
	eaClear(&s_ppNeedToStopSounds);
	eaClear(&s_ppPlayedSounds);
}

static void CutsceneStopAllFX()
{
	int i;
	for ( i=0; i < ea32Size(&s_pRunningFX); i++ ) {
		dtFxKill(s_pRunningFX[i]);
	}
	ea32Clear(&s_pRunningFX);
}

static void CutsceneSetEntityAlpha(Entity* e, F32 alpha){
	e->fAlpha = alpha;
	dtDrawSkeletonSetAlpha(e->dyn.guidDrawSkeleton, alpha, alpha);
}

static void CutsceneFreeAllClientEntities(SA_PARAM_OP_VALID CutsceneDef *pNextDef)
{
	int i;
	for (i = eaSize(&s_ppCOEnts) - 1; i >= 0; --i) 
	{
		Entity *pEntParent = entFromEntityRefAnyPartition(s_ppCOEnts[i]->oldEntityRef);
		CutsceneEntityList *pFoundList = NULL;

		// If the parent entity exists in the next cutscene, do not destroy the entity and reuse it
		if (pNextDef && 
			s_ppCOEnts[i]->oldEntityRef > 0 &&
			gclCutsceneParentEntityExistsInDef(pNextDef, s_ppCOEnts[i]->oldEntityRef, &pFoundList))
		{
			// Re-use this entity
			pFoundList->entRef = entGetRef(s_ppCOEnts[i]->entity);
			pFoundList->bCostumeLoaded = true;
			pFoundList->iFramesLoaded = 4;

			continue;
		}

		if(pEntParent) {
			pEntParent->bInCutscene = false;
			CutsceneSetEntityAlpha(pEntParent, 1);
		}
		gclClientOnlyEntityDestroy(&(s_ppCOEnts[i]));

		// Remove the entity from the list
		eaRemove(&s_ppCOEnts, i);
	}
}

static void gclCutsceneDefFreeDynamicData(CutsceneDef *pDef)
{
	int i, j;
	if(!pDef)
		return;

	for ( i=0; i < eaSize(&pDef->ppSubtitleLists); i++ ) {
		CutsceneSubtitleList *pList = pDef->ppSubtitleLists[i];
		for ( j=0; j < eaSize(&pList->ppSubtitlePoints); j++ ) {
			CutsceneSubtitlePoint *pPoint = pList->ppSubtitlePoints[j];
			smfblock_Destroy(pPoint->pTextBlock);
			pPoint->pTextBlock = NULL;
		}
	}
}

void gclCleanDynamicDataEx(CutsceneDef *pDef, CutsceneDef *pNextDef)
{
	CutsceneStopAllSounds();
	CutsceneStopAllFX();
	CutsceneFreeAllClientEntities(pNextDef);
	gclCutsceneDefFreeDynamicData(pDef);
	gclCutsceneSetFOV(gfxGetActiveCameraController(), gfxGetDefaultFOV());

	// Remove custom depth-of-field
	if(pOldSkyData)
	{
		if(pDef && pDef->pCutsceneDepthOfField)
		{
			CutsceneDOF* pCutDOF = pDef->pCutsceneDepthOfField;
			gfxSkyUnsetCustomDOF(pOldSkyData, pCutDOF->fade_out, pCutDOF->fade_out_rate);
		}
		else
		{
			gfxSkyUnsetCustomDOF(pOldSkyData, false, 0.0f);
		}
	}

	if(pDef && pDef->bHideAllPlayers){
		gclCutsceneSetAllPlayersAlpha(1, false);
	}

	if (pDef && pDef->bDisableCamLight) {
		gfxEnableCameraLight(true);
	}
}

void gclCleanDynamicData(CutsceneDef *pDef)
{
	gclCleanDynamicDataEx(pDef, NULL);
}

static void gclResetCutscene(void)
{
	if(g_ClientCutscene) {
		gclCleanDynamicData(g_ClientCutscene->pDef);
		StructDestroy(parse_ClientCutscene, g_ClientCutscene);
	}
	g_ClientCutscene = NULL;
	smfblock_Destroy(s_pSubtitleBlock);
	smfblock_Destroy(s_pTransTextBlock);
	s_pSubtitleBlock = NULL;
	s_pTransTextBlock = NULL;
}

void gclCutscenePathLoadSplines(CutscenePath *pPath)
{
	int i;
	if(pPath->ppPositions && pPath->smoothPositions)
	{
		if(pPath->pCamPosSpline)
			StructDestroy(parse_Spline, pPath->pCamPosSpline);
		pPath->pCamPosSpline = StructCreate(parse_Spline);
		for( i=0; i < eaSize(&pPath->ppPositions); i++ )
		{
			splineAppendCP(pPath->pCamPosSpline, pPath->ppPositions[i]->pos, pPath->ppPositions[i]->up, pPath->ppPositions[i]->tangent, 0, 0);
		}
	}
	if(pPath->ppTargets && pPath->smoothTargets)
	{
		if(pPath->pCamTargetSpline)
			StructDestroy(parse_Spline, pPath->pCamTargetSpline);
		pPath->pCamTargetSpline = StructCreate(parse_Spline);
		for( i=0; i < eaSize(&pPath->ppTargets); i++ )
		{
			splineAppendCP(pPath->pCamTargetSpline, pPath->ppTargets[i]->pos, pPath->ppTargets[i]->up, pPath->ppTargets[i]->tangent, 0, 0);
		}
	}
}

void gclCutsceneLoadSplines(CutscenePathList* pPathList)
{
	int i;
	for( i=0; i < eaSize(&pPathList->ppPaths); i++ )
	{
		gclCutscenePathLoadSplines(pPathList->ppPaths[i]);
	}
}

// TODO: don't want to have to load this explicitly
static void gclCutsceneLoadDef(CutsceneDef* newDef, CutsceneWorldVars *pUpdatedVars)
{
	int i, j;

	if(g_ClientCutscene)
		gclResetCutscene();

	g_ClientCutscene = StructCreate(parse_ClientCutscene);
	g_ClientCutscene->pDef = StructClone(parse_CutsceneDef, newDef);
	g_ClientCutscene->pWorldVars = StructClone(parse_CutsceneWorldVars,pUpdatedVars);

	if(g_ClientCutscene->pDef && g_ClientCutscene->pDef->pPathList)
		gclCutsceneLoadSplines(g_ClientCutscene->pDef->pPathList);

	// Complain if the user has given an invalid sound
	if(newDef->pchCutsceneSound && !sndEventExists(newDef->pchCutsceneSound)) {
		ErrorFilenamef(newDef->filename, "Couldn't find sound %s in cutscene %s\n", newDef->pchCutsceneSound, newDef->name);
	}
	for ( i=0; i < eaSize(&newDef->ppSoundLists); i++ ) {
		for ( j=0; j < eaSize(&newDef->ppSoundLists[i]->ppSoundPoints); j++ ) {
			const char *pcSoundName = newDef->ppSoundLists[i]->ppSoundPoints[j]->pSoundPath;
			if(pcSoundName && pcSoundName[0] && !sndEventExists(pcSoundName)) {
				ErrorFilenamef(newDef->filename, "Couldn't find sound %s in cutscene %s\n", pcSoundName, newDef->name);
			}
		}
	}

	sndSetCutsceneCropDistanceScalar(newDef->fSoundCropDistanceScalar);

	g_ClientCutscene->runningTime = cutscene_GetLength(g_ClientCutscene->pDef, true);
}

void gclCutsceneStartEx(Vec3 cameraPos, Vec3 cameraPyr, F32 fStartTime, ClientCutscene *pCutscene)
{
	// note that you don't want to make this large since the server doesn't know about it
	// as a result a player could get stuck watching a cutscene while a mob is beating on them
	s_fCutsceneAnimLoadWaitTime = 1.0f;
	s_bCutsceneAnimsLoaded = false;

	if(pCutscene)
	{
		pCutscene->elapsedTime = fStartTime;

		if (fStartTime > 0)
		{
			gclCutsceneBasicInitStartPos(pCutscene->pDef);
		}
	}

	prevPathIdx = prevPosIdx = prevTargIdx = 0;
	prevPosEaseOut = prevLookAtEaseOut = 1;
	prevElapsedTime = -0.001;

	if (fStartTime > 0)
	{
		gclGetCutsceneCameraPosPyr(0, cameraPos, cameraPyr, NULL);
	}

	// We want the first call called from the actualy camera controler to unset this flag
	firstCutsceneFrame = true;

	copyVec3(cameraPos, prevPos);
	copyVec3(cameraPyr, prevPyr);
}

void gclCutsceneStart(Vec3 cameraPos, Vec3 cameraPyr, F32 fStartTime)
{
	gclCutsceneStartEx(cameraPos, cameraPyr, fStartTime, g_ClientCutscene);
}

//if bEndNow is set, make sure the cutscene ends this frame
void gclCutsceneEndOnClient(bool bEndNow)
{
	F32 fGameCameraTweenTime = 0.0f;

	if(gbNoGraphics)
	{
		return;
	}

	if (!bEndNow && s_eDoorTransitionType == kDoorTransitionType_Departure)
	{
		s_fCutscenePauseThenEndTime = 5.0f;
		return;
	}

	s_fCutscenePauseThenEndTime = 0.0f;
	gGCLState.bLockPlayerAndCamera = false;
	globCmdParse("+ShowGameUI");
	if(!demo_playingBack())
	{
		UIGen *pCutsceneRoot = ui_GenFind("Cutscene_Root", kUIGenTypeNone);
		if (pCutsceneRoot)
			ui_GenSendMessage(pCutsceneRoot, "End");
		ui_RemoveActiveFamilies(UI_FAMILY_CUTSCENE);
	}

	if (g_ClientCutscene && g_ClientCutscene->pDef)
	{
		GfxCameraView *cam_view = gfxGetActiveCameraView();
		// End cutscene sound, if any
		if(g_ClientCutscene->pDef->pchCutsceneSound)
		{
			sndStopOneShot(g_ClientCutscene->pDef->pchCutsceneSound);
		}

		// Should the camera transition from its current state to the initial game camera state
		fGameCameraTweenTime = g_ClientCutscene->pDef->fGameCameraTweenTime;
	}

	smfblock_Destroy(s_pSubtitleBlock);
	smfblock_Destroy(s_pTransTextBlock);
	s_pSubtitleBlock = NULL;
	s_pTransTextBlock = NULL;

	gclSetGameCameraActive();
	gGCLState.bCutsceneActive = false;

	gclCamera_UpdateModeForRegion(gclCamera_GetEntity());

	if (fGameCameraTweenTime > FLT_EPSILON && 
		gGCLState.pPrimaryDevice && gGCLState.pPrimaryDevice->cutscenecamera.last_view)
	{
		Mat4 camera_matrix;
		CameraSettings* pSettings = gclCamera_GetSettings(&gGCLState.pPrimaryDevice->gamecamera);
		Vec3 vCutToGameCamDir;
		F32 fPitchTarget, fYawTarget, fPitchSource, fYawSource;
		F32 fPitch, fYaw, fPitchRate, fYawRate;
		F32 fMove, fMoveRate;

		gfxSetActiveCameraController(&gGCLState.pPrimaryDevice->gamecamera, true);
		gclCamera_Reset();
		gclDefaultCameraFunc(&gGCLState.pPrimaryDevice->gamecamera,gGCLState.pPrimaryDevice->gamecamera.last_view,100,100);

		subVec3(gGCLState.pPrimaryDevice->gamecamera.last_view->new_frustum.cammat[3],
			gGCLState.pPrimaryDevice->cutscenecamera.last_view->frustum.cammat[3],
			vCutToGameCamDir);

		fMove = normalVec3(vCutToGameCamDir);

		fPitchTarget = gGCLState.pPrimaryDevice->gamecamera.campyr[0];
		fYawTarget = gGCLState.pPrimaryDevice->gamecamera.campyr[1];
		fPitchSource = gGCLState.pPrimaryDevice->cutscenecamera.campyr[0];
		fYawSource = gGCLState.pPrimaryDevice->cutscenecamera.campyr[1];

		gfxCameraControllerCopyPosPyr(&gGCLState.pPrimaryDevice->cutscenecamera,&gGCLState.pPrimaryDevice->gamecamera);

		fPitch = MINF( ABS(fPitchTarget - fPitchSource), ABS(fPitchTarget+TWOPI - fPitchSource) );
		fYaw = MINF( ABS(fYawTarget - fYawSource), ABS(fYawTarget+TWOPI - fYawSource) );

		fPitchRate = fPitch / fGameCameraTweenTime;
		fYawRate = fYaw / fGameCameraTweenTime;
		fMoveRate = fMove / fGameCameraTweenTime;

		//compute the target dist and target py
		gGCLState.pPrimaryDevice->gamecamera.targetdist = fMove;
		gGCLState.pPrimaryDevice->gamecamera.targetpyr[0] = fPitchTarget;
		gGCLState.pPrimaryDevice->gamecamera.targetpyr[1] = fYawTarget;
		gGCLState.pPrimaryDevice->gamecamera.targetpyr[2] = 0;

		devassertmsg(FINITEVEC3(gGCLState.pPrimaryDevice->gamecamera.targetpyr), "Undefined camera PYR!");

		// somewhat of a hack to get back to a linear interpolation
		pSettings->fRotInterpNormMin = 1.f;
		pSettings->fRotInterpNormMax = 1.f;
		setVec3(pSettings->pyrInterpSpeed,fPitchRate,fYawRate,0);
		pSettings->fDistanceInterpSpeed = fMoveRate;

		createMat3YPR(camera_matrix, gGCLState.pPrimaryDevice->gamecamera.campyr);
		copyVec3(gGCLState.pPrimaryDevice->gamecamera.camcenter, camera_matrix[3]);
		copyVec3(gGCLState.pPrimaryDevice->gamecamera.camcenter, gGCLState.pPrimaryDevice->gamecamera.camfocus);
		gfxSetActiveCameraMatrix(camera_matrix,false);
		if (gclCamera_SetMode(kCameraMode_TweenToTarget, false))
		{
			copyVec3(vCutToGameCamDir,((TweenCameraSettings*)pSettings->pModeSettings)->vDir);
		}
	}

	// Wipe out the stored cutscene state
	gclResetCutscene();

	keybind_PopProfileName("Cutscene");

	if(s_finishedCB)
		s_finishedCB(s_finishedUserData);
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_NAME(CutsceneEndOnClient);
void gclCmdCutsceneEndOnClient(void)
{
	gclCutsceneEndOnClient(false);
}

static bool gclCutsceneFindStartEndIdx(void **points, F32 elapsedTime, F32 startTime, int* lastTargIdx /* out */, int* nextTargIdx /* out */, F32* weight /* out */)
{
	int curTarget;
	int maxTargets = eaSize(&points);

	// Find the two positions we're interpolating between
	for(curTarget=0; curTarget < maxTargets; curTarget++)
	{
		CutsceneCommonPointData *target = points[curTarget];
		CutsceneCommonPointData *prev = (curTarget ? points[curTarget-1] : NULL);

		if(elapsedTime < target->time)
		{
			F32 prevTime = (prev ? prev->time + prev->length : startTime);
			F32 moveTime = (target->time - prevTime);
			if(moveTime) {
				*lastTargIdx = curTarget-1;
				*nextTargIdx = curTarget;
				*weight = (elapsedTime - prevTime) / moveTime;
			} else {
				*lastTargIdx = curTarget;
				*nextTargIdx = curTarget;
				*weight = 1.0;
			}
			break;
		}
		if(elapsedTime < target->time + target->length)
		{
			*lastTargIdx = curTarget;
			*nextTargIdx = curTarget;
			*weight = 1.0f;
			break;
		}
	}

	if(curTarget >= maxTargets)
	{
		*lastTargIdx = maxTargets - 1;
		*nextTargIdx = maxTargets - 1;
		*weight = 1.0;
		return false;
	}

	*weight = CLAMP(*weight, 0, 1);
	return true;
}

typedef void (*gclCutsceneBlendCallback)(CutsceneFrameState *pState, void *pList, void *pPrev, void *pNext, F32 weight);
typedef void (*gclCutsceneRunCallback)(CutsceneFrameState *pState, void *pList, void *pPoint, F32 elapsedTime, CutsceneRunFunctionAction action);

static bool gclCutsceneApplyPoints(CutsceneFrameState *pState, void *pListIn, F32 prevTime, F32 elapsedTime, gclCutsceneBlendCallback blendF, gclCutsceneRunCallback runF)
{
	int lastTargIdx, nextTargIdx;
	CutsceneDummyTrack *pList = pListIn;
	F32 weight;
	bool found = false;


	if(!pList)
		return false;

	if(blendF) {
		if(gclCutsceneFindStartEndIdx(pList->ppGenPnts, elapsedTime, 0, &lastTargIdx, &nextTargIdx, &weight))
			found = true;
		blendF(pState, pList, lastTargIdx >= 0 ? pList->ppGenPnts[lastTargIdx] : NULL, nextTargIdx >= 0 ? pList->ppGenPnts[nextTargIdx] : NULL, weight);
	}

	if(runF) {
		int i;
		for ( i=0; i < eaSize(&pList->ppGenPnts); i++ ) {
			CutsceneCommonPointData *pPoint = pList->ppGenPnts[i];
			F32 pntStart = pPoint->time;
			F32 pntEnd = pPoint->time + pPoint->length;

			if(pntStart > prevTime && pntStart <= elapsedTime) {
				runF(pState, pList, pPoint, elapsedTime, CRFA_Start);
			}
			if(pntStart <= elapsedTime && pntEnd >= elapsedTime) {
				runF(pState, pList, pPoint, elapsedTime, CRFA_Mid);
			}
			if(pntEnd > prevTime && pntEnd <= elapsedTime) {
				runF(pState, pList, pPoint, elapsedTime, CRFA_End);
			}
			if(pntEnd > elapsedTime)
				found = true;
		}
	}

	return found;
}

static bool gclCutsceneApplyListsPoints(CutsceneFrameState *pState, void **ppLists, F32 prevTime, F32 elapsedTime, gclCutsceneBlendCallback applyF, gclCutsceneRunCallback runF)
{
	int i;
	bool bRet = false;
	for ( i=0; i < eaSize(&ppLists); i++ ) {
		if(gclCutsceneApplyPoints(pState, ppLists[i], prevTime, elapsedTime, applyF, runF))
			bRet = true;
	}
	return bRet;
}


// Returns false if we've run out of targets.  Sets target and weight
static bool gclFindCurrentPathTarget(CutscenePathPoint **targets, F32 elapsedTime, F32 startTime, bool prevEaseOut, int* lastTargIdx /* out */, int* nextTargIdx /* out */, F32* weight /* out */)
{
	bool easeOut, easeIn;

	if(!gclCutsceneFindStartEndIdx(targets, elapsedTime, startTime, lastTargIdx, nextTargIdx, weight))
		return false;

	easeOut = (*lastTargIdx < 0) ? prevEaseOut : targets[*lastTargIdx]->easeOut;
	easeIn = targets[*nextTargIdx]->easeIn;

	if(easeOut && easeIn)
	{
		F32 tempWeight = *weight;
		tempWeight = -2.0f*CUBE(tempWeight) + 3.0f*SQR(tempWeight);
		*weight = tempWeight;
	}
	else if(easeOut)
	{
		// Weight is w^2
		*weight = SQR(*weight);
	}
	else if(easeIn)
	{
		// Weight is 1 - w^2
		*weight = sqrt(*weight);
	}

	*weight = CLAMP(*weight, 0, 1);
	return true;
}

static void gclMakeCutsceneCamPYR(CutsceneFrameState *pState, Vec3 camPos, Vec3 targetPos, Vec3 camPyr /*out*/)
{
	Vec3 lookDirection;

	if(pState) {
		copyVec3(camPos, pState->vCamPos);
		copyVec3(targetPos, pState->vLookPos);
	}

	// These PYRs are actually backwards because the camera looks along the negative Z axis
	subVec3(camPos, targetPos, lookDirection);

	// Avoid divide-by-zero
	if(!vec3IsZero(lookDirection))
		orientYPR(camPyr, lookDirection);
}

static void interpPYRByQuat(F32 alpha, const Vec3 start, const Vec3 end, Vec3 result)
{
	Quat start_quat, end_quat, result_quat;

	if(alpha == 1.0f) {
		copyVec3(end, result);
		return;
	}

	if(alpha == 0.0f) {
		copyVec3(start, result);
		return;
	}

	PYRToQuat(start, start_quat);
	PYRToQuat(end, end_quat);
	quatInterp(alpha, start_quat, end_quat, result_quat);
	quatToPYR(result_quat, result);
}

static Entity* gclCutsceneGetPlayer()
{
	if(demo_playingBack())
		return demo_GetActivePlayer();
	return entActivePlayerPtr();
}
static Entity * gclCutsceneGetTeamSpokesmanByPlayer(SA_PARAM_NN_VALID Entity *pEnt)
{
	static Entity** s_eaTeamEnts = NULL;
	static TeamUpMember** s_eaTeamMembers = NULL;

	// Are we in a team dialog
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	if (pDialog && pDialog->iTeamSpokesmanID && pDialog->iTeamSpokesmanID != pEnt->myContainerID)
	{
		int i;
		int iCount;
		S32 iPartitionIdx = entGetPartitionIdx(pEnt);
		Team *pTeam = team_GetTeam(pEnt);

		// Find the team spokesman in the team
		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			if (pTeamMember->iEntID == pDialog->iTeamSpokesmanID)
			{
				return entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
			}
		}
		FOR_EACH_END

		eaClearFast(&s_eaTeamEnts);
		eaClearFast(&s_eaTeamMembers);

		iCount = TeamUp_GetTeamListSelfFirst(pEnt, &s_eaTeamEnts, &s_eaTeamMembers, -1, false, false);
		for(i=0; i < iCount; i++)
		{
			if(s_eaTeamMembers[i]->iEntID == pDialog->iTeamSpokesmanID)
			{
				return entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, s_eaTeamMembers[i]->iEntID);
			}
		}

		return NULL;
	}

	return pEnt;
}

static Entity * gclCutsceneGetTeamSpokesman()
{
	Entity *pPlayerEnt;
	if(demo_playingBack())
		return demo_GetActivePlayer();
	pPlayerEnt = entActivePlayerPtr();

	if (pPlayerEnt)
	{
		return gclCutsceneGetTeamSpokesmanByPlayer(pPlayerEnt);
	}

	return pPlayerEnt;
}

Entity* gclCutsceneGetCutsceneEntByName(CutsceneDef *pDef, const char *pcName)
{
	int i;
	for ( i=0; i < eaSize(&pDef->ppEntityLists); i++ ) {
		CutsceneEntityList *pList = pDef->ppEntityLists[i];
		if(stricmp_safe(pList->common.pcName, pcName) == 0) {
			return entFromEntityRefAnyPartition(pList->entRef);
		}
	}
	return NULL;
}

static bool gclCutsceneGetParentMat(CutsceneDef *pDef, CutsceneOffsetData *pOffset, Mat4 parentMat, bool bUseBoneRotation)
{
	CutsceneOffsetType offsetType = pOffset->offsetType;
	const char *pchCutsceneEntName = pOffset->pchCutsceneEntName;
	EntityRef entRef = pOffset->entRef;
	const char *pchBoneName = pOffset->pchBoneName;
	bool bFound = false;
	Entity *pEntity = NULL;

	switch(offsetType) {
	case CutsceneOffsetType_Player:
		pEntity = gclCutsceneGetPlayer();
		break;
	case CutsceneOffsetType_Actor:
		pEntity = entFromEntityRefAnyPartition(entRef);
		break;
	case CutsceneOffsetType_CutsceneEntity:
		pEntity = gclCutsceneGetCutsceneEntByName(pDef, pchCutsceneEntName);
		break;
	case CutsceneOffsetType_Contact:
		{
			ContactDialog *pContactDialog;
			pEntity = gclCutsceneGetPlayer();
			pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);		
			if (pContactDialog) {
				Entity *pCameraSourceEnt = entFromEntityRefAnyPartition(pContactDialog->cameraSourceEnt);
				if (pCameraSourceEnt) {
					pEntity = pCameraSourceEnt;
				} else if (vec3IsZero(pContactDialog->vecCameraSourcePos)) {
					pEntity = entFromEntityRefAnyPartition(pContactDialog->headshotEnt);
				} else {
					// Use specific source
					quatToMat(pContactDialog->quatCameraSourceRot, parentMat);
					copyVec3(pContactDialog->vecCameraSourcePos, parentMat[3]);
					bFound = true;
				}
			}
			break;
		}
	}

	if(!bFound && pEntity) {
		Vec2 py;
		ANALYSIS_ASSUME(pEntity != NULL);
		// we only want the yaw
		entGetFacePY(pEntity,py);
		py[0] = 0.0f;

		createMat3YP(parentMat,py);
		entGetPos(pEntity, parentMat[3]);

		if (bUseBoneRotation)
		{
			entGetPosForNextFrame(pEntity, parentMat[3]);
			entGetBoneMat(pEntity, pchBoneName, parentMat);
		}
		else
		{
			entGetBonePos(pEntity,pchBoneName,parentMat[3]);
		}

		bFound = true;
	}

	return bFound;
}

// this code is sort of duplicated in CutsceneEditor.c.  If you change it here, you should change it there
static bool gclCutsceneGetOffsetMat(CutsceneDef *pDef, CutsceneCommonTrackData *pCTD, Mat4 parentMat, bool bUseBoneRotation)
{
	//pCTD->bRelativePos can not be checked inside this function because camera paths dont have it set

	if(!gclCutsceneGetParentMat(pDef, &pCTD->main_offset, parentMat, bUseBoneRotation))
		return false;
	if(pCTD->bTwoRelativePos) {
		Vec3 dirVec;
		Mat4 tempMat;
		if(!gclCutsceneGetParentMat(pDef, &pCTD->second_offset, tempMat, bUseBoneRotation))
			return false;
		subVec3(tempMat[3], parentMat[3], dirVec);
		mat3FromFwdVector(dirVec, parentMat);
		addVec3(parentMat[3], tempMat[3], parentMat[3]);
		scaleVec3(parentMat[3], 0.5, parentMat[3]);
	}
	return true;
}

void gclCutsceneCGTParentMat(CutsceneDef *pDef, void *pCGT_In, Mat4 parentMat)
{
	CutsceneDummyTrack *pCGT = pCGT_In;
	copyMat4(unitmat, parentMat);
	if(pCGT->common.bRelativePos) {
		gclCutsceneGetOffsetMat(pDef, &pCGT->common, parentMat, true);
	}
}

static void gclCutsceneCGTPosPyr(CutsceneDef *pDef, void *pCGT_In, const Vec3 vPos_In, const Vec3 vPyr_In, Vec3 vPos, Vec3 vPyr)
{
	CutsceneDummyTrack *pCGT = pCGT_In;
	Mat4 mParentMat, mChildMat, mFinalMat;

	gclCutsceneCGTParentMat(pDef, pCGT, mParentMat);

	createMat3YPR(mChildMat, vPyr_In);
	mulMat3(mChildMat, mParentMat, mFinalMat);
	getMat3YPR(mFinalMat, vPyr);

	mulVecMat3(vPos_In, mParentMat, vPos);
	addVec3(vPos, mParentMat[3], vPos);
}

static bool gclGetCutsceneCameraPathPosPyr(CutsceneFrameState *pState, CutscenePath *pPath, F32 elapsedTime, F32 startTime, Vec3 cameraPos /*out*/, Vec3 cameraPYR /*out*/)
{
	F32 weight = 0;
	bool foundPos = false;
	bool foundTarget = false;
	int lastIdx = 0;
	int nextIdx = 0;

	if(eaSize(&pPath->ppPositions) > 0)
	{
		foundPos = gclFindCurrentPathTarget(pPath->ppPositions, elapsedTime, startTime, prevPosEaseOut, &lastIdx, &nextIdx, &weight);

		//Blend the Position
		if(pPath->type == CutscenePathType_ShadowEntity)
		{
			Mat4 shadowMat;
			if(gclCutsceneGetOffsetMat(pState->pDef, &pPath->common, shadowMat, false))
			{
				Vec3 relPos;
				Vec3 offset, secondPos;

				//Get the relative position
				if(lastIdx < 0 || lastIdx == nextIdx)
				{
					copyVec3(pPath->ppPositions[nextIdx]->pos, relPos);
				}
				else if(pPath->pCamPosSpline)
				{
					// Blend along curve
					Vec3 newUp, newDir;
					splineTransform(pPath->pCamPosSpline, lastIdx*3, weight, zerovec3, relPos, newUp, newDir, false);
				}
				else
				{
					// Blend along line
					interpVec3(weight, pPath->ppPositions[lastIdx]->pos, pPath->ppPositions[nextIdx]->pos, relPos);
				}
				//Get the absolute position
				mulVecMat3(relPos, shadowMat, offset);
				addVec3(shadowMat[3], offset, secondPos);
				if(g_CutsceneDrawDebugInfo)
					gfxDrawSphere3DARGB(secondPos, 1, 4, 0xFFAFAFFF, 0);
				//If we are blending from another path or a starting position
				if(lastIdx < 0) {
					//This takes the camera position and blends it based on a quaternion interpolation,
					//    the center of which is located at the look-at position. This only affects the camera's
					//    position, and not the orientation. The camera will rotate around the target to get to
					//    its final location.
					//    [DHOGBERG - 11/08/11]
					Vec3 lookAtPos;
					Vec3 v3PrevLookAtVec, v3FinalLookVec;
					Mat3 prevMat, finalMat;
					Quat qStart, qFinish, qInterp;
					F32 fRadius;
					Vec3 cameraLook, lookAtOffset;
					
					//Update the look-at position and look-at vectors
					copyVec3(pPath->ppTargets[nextIdx]->pos, lookAtPos);
					mulVecMat3(lookAtPos, shadowMat, lookAtOffset);
					addVec3(shadowMat[3], lookAtOffset, lookAtPos);

					subVec3(lookAtPos, secondPos, v3FinalLookVec);
					subVec3(lookAtPos, prevPos, v3PrevLookAtVec);

					//The radius is a basic linear interpolation between the initial camera distance from the lookAt and the final
					fRadius = interpF32(weight, distance3(lookAtPos,prevPos), distance3(lookAtPos, secondPos));

					//There is probably a faster way to convert a lookat vector into a quaternion
					mat3FromFwdVector(v3FinalLookVec, finalMat);
					mat3FromFwdVector(v3PrevLookAtVec, prevMat);
					mat3ToQuat(prevMat, qStart);
					mat3ToQuat(finalMat, qFinish);

					//This interpolates the angle at which the camera is positioned relative to the target
					quatInterp(weight, qStart, qFinish, qInterp);

					quatRotateVec3(qInterp, forwardvec, cameraLook);
					scaleVec3(cameraLook, fRadius, cameraLook);
					subVec3(lookAtPos, cameraLook, cameraPos);

					//This is how we would do a simple point to point linear interpolation of the camera position
					//interpVec3(weight, prevPos, secondPos, cameraPos);
				//Otherwise just set
				} else {
					copyVec3(secondPos, cameraPos);
				}
			}
		}
		else if(lastIdx < 0)
		{
			//Blend from last known position
			Vec3 secondPos;
			copyVec3(pPath->ppPositions[nextIdx]->pos, secondPos);
			interpVec3(weight, prevPos, secondPos, cameraPos);
		}
		else if(lastIdx == nextIdx || (pPath->type != CutscenePathType_Orbit && nearSameVec3(pPath->ppPositions[lastIdx]->pos, pPath->ppPositions[nextIdx]->pos)))
		{
			// Just Copy
			copyVec3(pPath->ppPositions[nextIdx]->pos, cameraPos);
		}
		else if(pPath->type == CutscenePathType_Orbit)
		{
			Vec3 posVec;
			F32 angle = pPath->angle * weight;
			F32 sint, cost;
			F32 temp;
			sincosf(angle, &sint, &cost);
			subVec3(pPath->ppPositions[0]->pos, pPath->ppTargets[0]->pos, posVec);
			temp = posVec[0]*cost - posVec[2]*sint;
			posVec[2] = posVec[2]*cost + posVec[0]*sint;
			posVec[0] = temp;
			addVec3(posVec, pPath->ppTargets[0]->pos, cameraPos);
		}
		else if(pPath->pCamPosSpline)
		{
			// Blend along curve
			Vec3 newPos, newUp, newDir;
			splineTransform(pPath->pCamPosSpline, lastIdx*3, weight, zerovec3, newPos, newUp, newDir, false);
			copyVec3(newPos, cameraPos);
		}
		else
		{
			// Blend along line
			interpVec3(weight, pPath->ppPositions[lastIdx]->pos, pPath->ppPositions[nextIdx]->pos, cameraPos);
		}
	}

	if(eaSize(&pPath->ppTargets) > 0)
	{
		foundTarget = gclFindCurrentPathTarget(pPath->ppTargets, elapsedTime, startTime, prevLookAtEaseOut, &lastIdx, &nextIdx, &weight);

		//Blend the PYR
		if(pPath->type == CutscenePathType_WatchEntity || pPath->type == CutscenePathType_ShadowEntity)
		{
			Mat4 shadowMat;
			if(gclCutsceneGetOffsetMat(pState->pDef, &pPath->common, shadowMat, pPath->type != CutscenePathType_ShadowEntity))
			{
				Vec3 relPos;
				Vec3 offset, lookAtPos;
				//Get the relative position
				if(lastIdx < 0 || lastIdx == nextIdx)
				{
					copyVec3(pPath->ppTargets[nextIdx]->pos, relPos);
				}
				else if(pPath->pCamTargetSpline)
				{
					// Blend along curve
					Vec3 newUp, newDir;
					splineTransform(pPath->pCamTargetSpline, lastIdx*3, weight, zerovec3, relPos, newUp, newDir, false);
				}
				else
				{
					// Blend along line
					interpVec3(weight, pPath->ppTargets[lastIdx]->pos, pPath->ppTargets[nextIdx]->pos, relPos);
				}
				mulVecMat3(relPos, shadowMat, offset);
				addVec3(shadowMat[3], offset, lookAtPos);
				if(g_CutsceneDrawDebugInfo)
					gfxDrawSphere3DARGB(lookAtPos, 1, 4, 0xFFFFAFAF, 0);
				//If we are blending from another path
				if(lastIdx < 0)
				{
					Vec3 secondPyr;
					gclMakeCutsceneCamPYR(pState, cameraPos, lookAtPos, secondPyr);
					interpPYRByQuat(weight, prevPyr, secondPyr, cameraPYR);
				}
				//Otherwise just set
				else
				{
					gclMakeCutsceneCamPYR(pState, cameraPos, lookAtPos, cameraPYR);
				}
			}
		}
		else if(lastIdx < 0)
		{
			Vec3 secondPyr;
			gclMakeCutsceneCamPYR(pState, cameraPos, pPath->ppTargets[nextIdx]->pos, secondPyr);
			interpPYRByQuat(weight, prevPyr, secondPyr, cameraPYR);
		}
		else if(lastIdx == nextIdx || (pPath->type != CutscenePathType_LookAround && nearSameVec3(pPath->ppTargets[lastIdx]->pos, pPath->ppTargets[nextIdx]->pos)))
		{
			gclMakeCutsceneCamPYR(pState, cameraPos, pPath->ppTargets[nextIdx]->pos, cameraPYR);
		}
		else if(pPath->type == CutscenePathType_LookAround)
		{
			Vec3 lookVec;
			F32 angle = pPath->angle * weight;
			F32 sint, cost;
			F32 temp;
			sincosf(angle, &sint, &cost);
			subVec3(pPath->ppTargets[0]->pos, pPath->ppPositions[0]->pos, lookVec);
			temp = lookVec[0]*cost - lookVec[2]*sint;
			lookVec[2] = lookVec[2]*cost + lookVec[0]*sint;
			lookVec[0] = temp;
			addVec3(lookVec, cameraPos, lookVec);
			gclMakeCutsceneCamPYR(pState, cameraPos, lookVec, cameraPYR);
		}
		else if(pPath->pCamTargetSpline)
		{
			Vec3 newPos, newUp, newDir;
			splineTransform(pPath->pCamTargetSpline, lastIdx*3, weight, zerovec3, newPos, newUp, newDir, false);
			gclMakeCutsceneCamPYR(pState, cameraPos, newPos, cameraPYR);
		}
		else
		{
			Vec3 lookAtPos;
			interpVec3(weight, pPath->ppTargets[lastIdx]->pos, pPath->ppTargets[nextIdx]->pos, lookAtPos);
			gclMakeCutsceneCamPYR(pState, cameraPos, lookAtPos, cameraPYR);
		}
	}

	return (foundPos || foundTarget);
}

static void gclCutsceneBlendFade(CutsceneFrameState *pState, CutsceneFadeList *pList, CutsceneFadePoint *pPrev, CutsceneFadePoint *pNext, F32 weight)
{
	Vec4 vFadeValue;
	bool bAdditive = false;

	if(!pNext)
		return;

	if(!pPrev) {
		copyVec4(pNext->vFadeValue, vFadeValue);
	} else {
		lerpVec4(pNext->vFadeValue, weight, pPrev->vFadeValue, vFadeValue);
		bAdditive = (pPrev->bAdditive && pNext->bAdditive);
	}

	if(vFadeValue[3] > 0.0f) {
		int c;
		int rgba=0;
		for ( c=0; c < 4; c++ )
		{
			U8 val = vFadeValue[3-c]*255.0f;
			rgba |= (val<<(8*c));
		}				
		{
			AtlasTex *white = atlasLoadTexture("white");
			display_sprite_4Color(white, 0, 0, UI_UI2LIB_Z-3, 1000, 1000, rgba, rgba, rgba, rgba, bAdditive);
		}
	}
}

static void gclCutsceneBlendDOF(CutsceneFrameState *pState, CutsceneDOFList *pList, CutsceneDOFPoint *pPrev, CutsceneDOFPoint *pNext, F32 weight)
{	
	if(pState->pSkyData && pPrev && pNext && pPrev->bDOFIsOn && pNext->bDOFIsOn) {
		F32 fDOFBlur = lerp(pPrev->fDOFBlur, pNext->fDOFBlur, weight);
		F32 fDOFDistDiff = lerp(pPrev->fDOFDist, pNext->fDOFDist, weight);
		F32 fDOFMidDist = distance3(pState->vCamPos, pState->vLookPos);
		gfxSkySetCustomDOF(pState->pSkyData, MAX(fDOFMidDist-fDOFDistDiff, 0.0f), fDOFBlur, fDOFMidDist, 0, fDOFMidDist+fDOFDistDiff, fDOFBlur, false, 0.0f);
	} else if (pState->pSkyData) {
		gfxSkyUnsetCustomDOF(pState->pSkyData, false, 0.0f);
	}
}

static void gclCutsceneBlendFOV(CutsceneFrameState *pState, CutsceneFOVList *pList, CutsceneFOVPoint *pPrev, CutsceneFOVPoint *pNext, F32 weight)
{	
	F32 prevFOV, nextFOV, resultFOV;
	F32 defaultFov = gfxGetDefaultFOV();
	if(!pState->pCamera)
		return;

	prevFOV = (pPrev ? pPrev->fFOV : defaultFov);
	nextFOV = (pNext ? pNext->fFOV : defaultFov);
	resultFOV = lerp(prevFOV, nextFOV, weight);

	gclCutsceneSetFOV(pState->pCamera, resultFOV);
}

static void gclCutsceneAddPetsFromEntity(Entity *pEnt, Entity ***pppEntList)
{
	int j;
	S32 iOwnedPetsSize;
	S32 iCritterPetsSize;
	int iPartitionIdx;

	iOwnedPetsSize = ea32Size(&pEnt->pSaved->ppAwayTeamPetID);
	iCritterPetsSize = eaSize(&pEnt->pSaved->ppCritterPets);
	iPartitionIdx = entGetPartitionIdx(pEnt);

	for ( j = 0; j < iOwnedPetsSize; j++ ) {
		Entity *pPetEnt = entFromContainerID(iPartitionIdx,GLOBALTYPE_ENTITYSAVEDPET,pEnt->pSaved->ppAwayTeamPetID[j]);
		if (pPetEnt) {
			eaPush(pppEntList, pPetEnt);
		}
	}

	for ( j = 0; j < iCritterPetsSize; j++ )
	{
		Entity* pCritEnt = entFromEntityRefAnyPartition(pEnt->pSaved->ppCritterPets[j]->erPet);
		if (pCritEnt) {
			eaPush(pppEntList, pCritEnt);
		}
	}
}

static void gclCutsceneGetPetList(Entity *pPlayer, Entity ***pppEntList)
{	
	Team *pTeam;
	S32 iPartitionIdx;

	if(!pPlayer || !pPlayer->pSaved)
		return;

	// Add pets from the player
	gclCutsceneAddPetsFromEntity(pPlayer, pppEntList);

	pTeam = team_GetTeam(pPlayer);

	if (pTeam)
	{
		iPartitionIdx = entGetPartitionIdx(pPlayer);

		FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
		{
			if (pTeamMember && pTeamMember->iEntID != pPlayer->myContainerID)
			{
				Entity *pTeamMemberEnt = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID);
				if (pTeamMemberEnt)
				{
					// Add pets from the team member
					gclCutsceneAddPetsFromEntity(pTeamMemberEnt, pppEntList);
				}
			}
		}
		FOR_EACH_END
	}
}

static void gclCutsceneGetTeamList(Entity *pPlayer, Entity ***pppEntList)
{
	int i;
	Team *pTeam;
	Entity *pEntSpokesman = gclCutsceneGetTeamSpokesman();

	if(!pPlayer)
		return;

	// Team spokesman is different than the player. Show player in the team member list
	if (pPlayer != pEntSpokesman)
	{
		eaPush(pppEntList, pPlayer);
	}

	pTeam = team_GetTeam(pPlayer);
	if(pTeam) {
		int iPartitionIdx = entGetPartitionIdx(pPlayer);
		for ( i=0; i < eaSize(&pTeam->eaMembers); i++ ) {
			Entity *pMember = entFromContainerID(iPartitionIdx, GLOBALTYPE_ENTITYPLAYER, pTeam->eaMembers[i]->iEntID);
			if(pMember && pMember != pPlayer && pMember != pEntSpokesman) 
			{
				eaPush(pppEntList, pMember);
			}
		}
	}

	if (pPlayer->pTeamUpRequest)
	{
		TeamUp_AddTeamToEntityList(pPlayer, pPlayer->pTeamUpRequest->ppGroups, pppEntList, pEntSpokesman, false);
	}

	gclCutsceneGetPetList(pPlayer, pppEntList);
}

void gclCutsceneSetAllPlayersAlpha( F32 fAlpha, bool bNoInterpAlpha )
{
	EntityIterator* iter = NULL;
	Entity *pEnt;

	iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	while ((pEnt = EntityIteratorGetNext(iter))) {
		if (pEnt && entIsPlayer(pEnt)) {
			CutsceneSetEntityAlpha(pEnt, fAlpha);
			pEnt->bPreserveAlpha = true;
			pEnt->bNoInterpAlpha = bNoInterpAlpha;
		}
	}
	EntityIteratorRelease(iter);

	iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYSAVEDPET);
	while ((pEnt = EntityIteratorGetNext(iter))) {
		if(pEnt) {
			Entity *pOwner = entFromEntityRefAnyPartition(pEnt->erOwner);
			if (pOwner && entIsPlayer(pOwner)) {
				CutsceneSetEntityAlpha(pEnt, fAlpha);
				pEnt->bPreserveAlpha = true;
				pEnt->bNoInterpAlpha = bNoInterpAlpha;
			}
		}
	}
	EntityIteratorRelease(iter);

	iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYCRITTER);
	while ((pEnt = EntityIteratorGetNext(iter))) {
		if(pEnt) {
			Entity *pOwner = entFromEntityRefAnyPartition(pEnt->erOwner);
			if (pOwner && entIsPlayer(pOwner)) {
				CutsceneSetEntityAlpha(pEnt, fAlpha);
				pEnt->bPreserveAlpha = true;
				pEnt->bNoInterpAlpha = bNoInterpAlpha;
			}
		}
	}
	EntityIteratorRelease(iter);
}

static Entity* gclCutsceneGetEntityFromType(CutsceneEntityList *pList)
{
	Entity *pRetEnt = NULL;
	switch(pList->entityType) {
		xcase CutsceneEntityType_Player:
		{
			pRetEnt = gclCutsceneGetPlayer();
		}
		xcase CutsceneEntityType_TeamSpokesman:
		{
			pRetEnt = gclCutsceneGetTeamSpokesman();
		}
		xcase CutsceneEntityType_TeamMember:
		{
			Entity **ppTeamEnts = NULL;
			Entity *pPlayer = gclCutsceneGetPlayer();

			gclCutsceneGetTeamList(pPlayer, &ppTeamEnts);

			if(pList->EntityIdx >= 0 && pList->EntityIdx < eaSize(&ppTeamEnts)) {
				pRetEnt = ppTeamEnts[pList->EntityIdx];
			}

			eaDestroy(&ppTeamEnts);
		}
		xcase CutsceneEntityType_Actor:
		{
			pRetEnt = entFromEntityRefAnyPartition(pList->entActorRef);
		}
	}
	return pRetEnt;
}

static bool gclCutsceneParentEntityExistsInDef(SA_PARAM_NN_VALID CutsceneDef *pDef, EntityRef erParentEnt, CutsceneEntityList **pListFound)
{
	if (pDef)
	{
		FOR_EACH_IN_EARRAY(pDef->ppEntityLists, CutsceneEntityList, pList)
		{
			if(pList && 
				(pList->entityType == CutsceneEntityType_Player ||
				pList->entityType == CutsceneEntityType_TeamSpokesman ||
				pList->entityType == CutsceneEntityType_TeamMember)) 
			{
				Entity *pCurrentEnt = gclCutsceneGetEntityFromType(pList);
				if (pCurrentEnt && entGetRef(pCurrentEnt) == erParentEnt)
				{
					if (pListFound)
					{
						*pListFound = pList;
					}
					return true;
				}
			}
		}
		FOR_EACH_END
	}
	return false;
}

static CostumeDisplayData* gclDemoPlayback_GetFakeItemDisplayData(Entity* pEnt, CostumeRefWrapper** eaCostumes, kCostumeDisplayMode eMode, ItemCategory* eaiCategories, SlotType eEquippedSlot)
{
	CostumeDisplayData *pData;
	int iCat, iCostume;

	pData = calloc(1, sizeof(CostumeDisplayData));

	if (!pData) {
		return NULL;
	}

	pData->iPriority = DEFAULT_ITEM_OVERLAY_PRIORITY;
	pData->eMode = eMode;

	for (iCat = 0; iCat < eaiSize(&eaiCategories); iCat++)
	{
		ItemCategory eCat = eaiCategories[iCat];
		if (eCat >= kItemCategory_FIRST_DATA_DEFINED &&
			g_ItemCategoryNames.ppInfo[eCat-kItemCategory_FIRST_DATA_DEFINED]->pchStanceWords)
			eaPush(&pData->eaStances, g_ItemCategoryNames.ppInfo[eCat-kItemCategory_FIRST_DATA_DEFINED]->pchStanceWords);
	}

	for (iCostume = 0; iCostume < eaSize(&eaCostumes); iCostume++)
	{
		PlayerCostume* pCostume = GET_REF(eaCostumes[iCostume]->hCostume);
		SpeciesDef* pSpecies = pCostume ? GET_REF(pCostume->hSpecies) : NULL;
		ItemCategoryAdditionalCostumeBone** eaAddBones = NULL;
		NOCONST(PlayerCostume)* pCostumeClone = NULL;
		if (pCostume && (!gConf.bCheckOverlayCostumeSpecies || !pSpecies || !pEnt || !pEnt->pChar || (pSpecies == GET_REF(pEnt->pChar->hSpecies))))
		{
			if (g_CostumeConfig.bEnableItemCategoryAddedBones)
			{
				int iCatBone, iPart;
				for (iCat = 0; iCat < eaiSize(&eaiCategories); iCat++)
				{
					if (eaiCategories[iCat] >= kItemCategory_FIRST_DATA_DEFINED)
					{
						ItemCategoryInfo* pCatInfo = g_ItemCategoryNames.ppInfo[eaiCategories[iCat] - kItemCategory_FIRST_DATA_DEFINED];
						if (pCatInfo)
						{
							if (eEquippedSlot == kSlotType_Primary)
								eaAddBones = pCatInfo->eaPrimarySlotAdditionalBones;
							else
								eaAddBones = pCatInfo->eaSecondarySlotAdditionalBones;

							for (iCatBone = 0; iCatBone < eaSize(&eaAddBones); iCatBone++)
							{
								for (iPart = 0; iPart < eaSize(&pCostume->eaParts); iPart++)
								{
									if (REF_COMPARE_HANDLES(pCostume->eaParts[iPart]->hBoneDef, eaAddBones[iCatBone]->hOldBone))
									{
										PCBoneDef* pOldBone = GET_REF(pCostume->eaParts[iPart]->hBoneDef);
										//found a match
										if (!pCostumeClone)
											pCostumeClone = StructCloneDeConst(parse_PlayerCostume, pCostume);

										if (eaAddBones[iCatBone]->eType == kAdditionalCostumeBoneType_Move)
										{
											COPY_HANDLE(pCostumeClone->eaParts[iPart]->hBoneDef, eaAddBones[iCatBone]->hNewBone);
											pCostumeClone->eaParts[iPart]->pchOrigBone = allocAddString(pOldBone->pcBoneName);
										}
										else if (eaAddBones[iCatBone]->eType == kAdditionalCostumeBoneType_Clone)
										{
											NOCONST(PCPart)* pPartClone = StructCloneNoConst(parse_PCPart, pCostumeClone->eaParts[iPart]);
											pPartClone->pchOrigBone = allocAddString(pOldBone->pcBoneName);
											COPY_HANDLE(pPartClone->hBoneDef, eaAddBones[iCatBone]->hNewBone);
											eaPush(&pCostumeClone->eaParts, pPartClone);
										}
									}
								}
							}
						}
					}
				}
			}
			if (!pCostumeClone)
				eaPush(&pData->eaCostumes, pCostume);
			else
				eaPush(&pData->eaCostumesOwned, CONTAINER_RECONST(PlayerCostume, pCostumeClone));
		}
	}
	if (pData->eaCostumes && eaSize(&pData->eaCostumes) == 0)
		eaDestroy(&pData->eaCostumes);

	if (pData->eaCostumesOwned && eaSize(&pData->eaCostumesOwned) == 0)
		eaDestroyStruct(&pData->eaCostumesOwned, parse_PlayerCostume);

	if (!pData->eaCostumesOwned && !pData->eaCostumes)
		SAFE_FREE(pData);

	return pData;
}

static Entity* gclCutsceneInitClientEntity(CutsceneEntityList *pList)
{
	ClientOnlyEntity *pCOEnt = gclClientOnlyEntityCreate(true);
	NOCONST(Entity) *pNoConstEnt =CONTAINER_NOCONST(Entity, (pCOEnt->entity));
	CostumeDisplayData** eaDisplayData = NULL;
	Entity *pEnt = pCOEnt->entity;
	char cBuffer[256];
	int i;

	if (eaSize(&pList->eaOverrideEquipment) > 0)
		eaStackCreate(&eaDisplayData, eaSize(&pList->eaOverrideEquipment));

	pCOEnt->isCutsceneEnt = true;
	pCOEnt->noAutoFree = true;
	eaPush(&s_ppCOEnts, pCOEnt);
	pEnt->fEntitySendDistance = ENTITY_DEFAULT_SEND_DISTANCE;
	CutsceneSetEntityAlpha(pEnt, 0);

	sprintf(cBuffer, "%s-Location", __FUNCTION__);
	pEnt->dyn.guidLocation = dtNodeCreate();
	dtNodeSetTag(pEnt->dyn.guidLocation, allocAddString(cBuffer));

	sprintf(cBuffer, "%s-Root", __FUNCTION__);
	pEnt->dyn.guidRoot = dtNodeCreate();
	dtNodeSetTag(pEnt->dyn.guidRoot, allocAddString(cBuffer));

	mmCreate(&pEnt->mm.movement, gclEntityMovementManagerMsgHandler, pEnt, entGetRef(pEnt), 0, zerovec3, worldGetActiveColl(PARTITION_CLIENT));
	mrSurfaceCreate(pEnt->mm.movement, &pEnt->mm.mrSurface);
	mrSurfaceSetDefaults(pEnt->mm.mrSurface);

	pList->entRefParent = 0;

	for (i = 0; i < eaSize(&pList->eaOverrideEquipment); i++)
	{
		ItemDef* pDef = GET_REF(pList->eaOverrideEquipment[i]->hItem);
		if (pDef)
		{
			CostumeDisplayData* pNewData = item_GetCostumeDisplayData(PARTITION_CLIENT, pCOEnt->entity, NULL, pDef, pList->eaOverrideEquipment[i]->eSlot, NULL, 0);
			eaPush(&eaDisplayData, pNewData);
		}
		//during demoplayback we probably won't have the item def
		else
		{
			CostumeDisplayData* pNewData = gclDemoPlayback_GetFakeItemDisplayData(pEnt, pList->eaOverrideEquipment[i]->eaCostumes, pList->eaOverrideEquipment[i]->eMode, pList->eaOverrideEquipment[i]->eaiCategories, pList->eaOverrideEquipment[i]->eSlot);
			eaPush(&eaDisplayData, pNewData);
		}
	}

	if(pList->entityType == CutsceneEntityType_Custom) {
		if (eaSize(&eaDisplayData) == 0)
		{
			REF_HANDLE_COPY(pEnt->costumeRef.hReferencedCostume, pList->hCostume);
		}
		else
		{
			//If we have equipment to apply, we need to make a costume clone anyway.
			PlayerCostume *pCostume = GET_REF(pList->hCostume);
			NOCONST(PlayerCostume)* pNewCostume = NULL;
			if(pCostume)
				pNewCostume = costumeTailor_ApplyOverrideSet(pCostume, NULL, eaDisplayData, NULL);
			
			if (pNewCostume)
			{
				pEnt->costumeRef.pEffectiveCostume = (PlayerCostume*)pNewCostume;
			}
			else
			{
				REF_HANDLE_COPY(pEnt->costumeRef.hReferencedCostume, pList->hCostume);
			}
		}
	} else {
		Entity *pParentEnt = gclCutsceneGetEntityFromType(pList);
		if(pParentEnt) 
		{
			Quat tempQuat;
			PlayerCostume **eaUnlockedCostumes = NULL;
			const char **eapchPowerFXBones = NULL;
			GameAccountDataExtract *pExtract = entity_GetCachedGameAccountDataExtract(pParentEnt);
			PCSlotType *pSlotType = NULL;
			NOCONST(PlayerCostume)* pNewCostume = NULL;

			pCOEnt->oldEntityRef = pParentEnt->myRef;
			pList->entRefParent = pParentEnt->myRef;

			if(pParentEnt->pChar) {
				pNoConstEnt->pChar = StructCreateNoConst(parse_Character);
				character_Reset(entGetPartitionIdx(pEnt), pEnt->pChar, pEnt, NULL);
				REF_HANDLE_COPY(pNoConstEnt->pChar->hSpecies, pParentEnt->pChar->hSpecies);
				REF_HANDLE_COPY(pNoConstEnt->pChar->hClass, pParentEnt->pChar->hClass);
			}

			mmGetPositionFG(pParentEnt->mm.movement, pList->vPrevPos);
			mmGetRotationFG(pParentEnt->mm.movement, tempQuat);
			quatToPYR(tempQuat, pList->vPrevRot);

			//Copy Costume
			if(pList->charClassType != CharClassTypes_None) {
				Entity *pPuppetEnt = Entity_FindCurrentOrPreferredPuppet(pParentEnt, pList->charClassType);
				if(pPuppetEnt) {
					PlayerCostume *pCostume = costumeEntity_GetActiveSavedCostume(pPuppetEnt);
					if(pCostume)
						pNewCostume = costumeTailor_ApplyOverrideSet(pCostume, NULL, eaDisplayData, NULL);
					if (pPuppetEnt && pPuppetEnt->pSaved && pPuppetEnt->pSaved->costumeData.eaCostumeSlots && pPuppetEnt->pSaved->costumeData.eaCostumeSlots[0])
						pSlotType = costumeLoad_GetSlotType(pPuppetEnt->pSaved->costumeData.eaCostumeSlots[0]->pcSlotType);

					if (pNewCostume && pPuppetEnt)
					{
						if (pPuppetEnt->pSaved)
							costumeEntity_GetUnlockCostumes(pPuppetEnt->pSaved->costumeData.eaUnlockedCostumeRefs, pPuppetEnt->pPlayer && pPuppetEnt->pPlayer->pPlayerAccountData ? GET_REF(pPuppetEnt->pPlayer->pPlayerAccountData->hData) : NULL, pPuppetEnt, pPuppetEnt, &eaUnlockedCostumes);
						entity_FindPowerFXBones(pPuppetEnt,&eapchPowerFXBones);

						costumeTailor_FillAllBones(pNewCostume, pPuppetEnt->pChar ? GET_REF(pPuppetEnt->pChar->hSpecies) : NULL, NULL, pSlotType, true, false, true);
						pEnt->costumeRef.pEffectiveCostume = (PlayerCostume*)pNewCostume;
					}
				}
			} else {
				PlayerCostume *pCostume = costumeEntity_GetEffectiveCostume(pParentEnt);
				if(pCostume)
					pNewCostume = costumeTailor_ApplyOverrideSet(pCostume, pSlotType, eaDisplayData, NULL);
				if (pParentEnt && pParentEnt->pSaved && pParentEnt->pSaved->costumeData.eaCostumeSlots && pParentEnt->pSaved->costumeData.eaCostumeSlots[0])
					pSlotType = costumeLoad_GetSlotType(pParentEnt->pSaved->costumeData.eaCostumeSlots[0]->pcSlotType);

				if (pNewCostume)
				{
					if (pParentEnt->pSaved)
						costumeEntity_GetUnlockCostumes(pParentEnt->pSaved->costumeData.eaUnlockedCostumeRefs, pParentEnt->pPlayer && pParentEnt->pPlayer->pPlayerAccountData ? GET_REF(pParentEnt->pPlayer->pPlayerAccountData->hData) : NULL, pParentEnt, pParentEnt, &eaUnlockedCostumes);
					entity_FindPowerFXBones(pParentEnt,&eapchPowerFXBones);

					costumeTailor_FillAllBones(pNewCostume, pParentEnt->pChar ? GET_REF(pParentEnt->pChar->hSpecies) : NULL, NULL, pSlotType, true, false, true);
					pEnt->costumeRef.pEffectiveCostume = (PlayerCostume*)pNewCostume;
				}
			}
			// Copy FX
			if(pList->bPreserveMovementFX)
				mmrFxCopyAllFromManager(pEnt->mm.movement, pParentEnt->mm.movement);
		}
		else if (pList->entityType != CutsceneEntityType_Custom)
		{
			// If the parent entity is not found mark the costume as loaded
			pList->bCostumeLoaded = true;
		}
	}
	
	for (i = 0; i < eaSize(&eaDisplayData); i++)
	{
		free(eaDisplayData[i]);
	}

	return pEnt;
}

static bool gclCutsceneEntityOncePerFrame(CutsceneDef *pDef)
{
	int i;
	bool bLoaded = true;
	for ( i=0; i < eaSize(&pDef->ppEntityLists); i++ ) {
		CutsceneEntityList *pList = pDef->ppEntityLists[i];
		Entity *pEnt = entFromEntityRefAnyPartition(pList->entRef);
		if(!pEnt){
			continue;
		}

		if(!pList->bCostumeLoaded) {
			WLCostume *hWLCostume;

			costumeGenerate_FixEntityCostume(pEnt);
			if (pEnt->dyn.guidDrawSkeleton) {
				dtDrawSkeletonDestroy(pEnt->dyn.guidDrawSkeleton);
				pEnt->dyn.guidDrawSkeleton = 0;
			}
			if (pEnt->dyn.guidSkeleton) {
				dtSkeletonDestroy(pEnt->dyn.guidSkeleton);
				pEnt->dyn.guidSkeleton = 0;
			}
			entClientCreateSkeleton(pEnt);

			hWLCostume = GET_REF(pEnt->hWLCostume);
			if(hWLCostume && hWLCostume->bComplete) {
				pList->bCostumeLoaded = true;
			}
			CutsceneSetEntityAlpha(pEnt, 0);
			bLoaded = false;
		}else{
			DynDrawSkeleton *pSkeleton = dynDrawSkeletonFromGuid(pEnt->dyn.guidDrawSkeleton);
			if(pSkeleton) {
				gfxEnsureAssetsLoadedForSkeleton(pSkeleton);
			}
			if(pList->iFramesLoaded < 3) {
				CutsceneSetEntityAlpha(pEnt, 0);
				pList->iFramesLoaded++;
				bLoaded = false;
			} else {
				Entity *pEntParent = entFromEntityRefAnyPartition(pList->entRefParent);
				if(pEntParent) {
					pEntParent->bInCutscene = true;
					CutsceneSetEntityAlpha(pEntParent, 0);
				}
				CutsceneSetEntityAlpha(pEnt, 1);
			}
		}
	}
	return bLoaded;
}

static void gclCutsceneRunCameraShake(CutsceneFrameState *pState, CutsceneShakeList *pList, CutsceneShakePoint *pPoint, F32 elapsedTime, CutsceneRunFunctionAction action)
{
	switch(action) 
	{
		xcase CRFA_Start:
	gclCamera_Shake(pPoint->common.length, pPoint->fMagnitude, pPoint->fVertical, pPoint->fPan, 1.0f);
	xcase CRFA_Mid:
	//Do nothing
	xcase CRFA_End:
	//Do nothing
	break;
	}
}

static void gclCutsceneRunUIGen(CutsceneFrameState *pState, CutsceneUIGenList *pList, CutsceneUIGenPoint *pPoint, F32 elapsedTime, CutsceneRunFunctionAction action)
{
	UIGen *pCutsceneGen = NULL;
	switch(action) 
	{
		xcase CRFA_Start:
			pCutsceneGen = ui_GenFind("Cutscene_Root", kUIGenTypeNone);
			if(!pCutsceneGen)
			{
				ErrorFilenamef(pState->pDef->filename, "Cutscene has a UI Gen Track but there was no 'Cutscene_Root' UI Gen found.");
				return;
			}

			if(pPoint->actionType == CutsceneUIGenAction_MessageAndVariable || pPoint->actionType == CutsceneUIGenAction_VariableOnly)
				if(pPoint->pcVariable && pPoint->pcVariable[0])
				{
					const char *pcStringValue = gclCutsceneGetTranslatedMessageForUIGen(pState, pPoint);
					if(!pcStringValue || pcStringValue[0] == '\0')
						pcStringValue = pPoint->pcStringValue;

					if(!ui_GenSetVarEx(pCutsceneGen, pPoint->pcVariable, pPoint->fFloatValue, pcStringValue, false))
						ErrorFilenamef(pState->pDef->filename, "Cutscene has a UI Gen Track trying to set a variable that does not exist in the 'Cutscene_Root' UI Gen: %s", pPoint->pcVariable);
				}

			if(pPoint->actionType == CutsceneUIGenAction_MessageAndVariable || pPoint->actionType == CutsceneUIGenAction_MessageOnly)
				if(pPoint->pcMessage && pPoint->pcMessage[0])
					ui_GenSendMessage(pCutsceneGen, pPoint->pcMessage);
		xcase CRFA_Mid:
			//Do nothing
		xcase CRFA_End:
			//Do nothing
		break;
	}
}

static void gclCutsceneBlendObject(CutsceneFrameState *pState, CutsceneObjectList *pList, CutsceneObjectPoint *pPrev, CutsceneObjectPoint *pNext, F32 weight)
{
	GroupDef *pGroupDef;
	if(!pList->pcObjectName || !pList->pcObjectName[0])
		return;

	pGroupDef = objectLibraryGetGroupDefByName(pList->pcObjectName, true);
	if(pGroupDef) {
		TempGroupParams tgparams = {0};
		Vec3 vPrevPos, vPrevPyr;
		Vec3 vNextPos, vNextPyr;
		Vec3 blendedPyr;
		Mat4 objMat;

		if(pPrev)
			gclCutsceneCGTPosPyr(pState->pDef, pList, pPrev->vPosition, pPrev->vRotation, vPrevPos, vPrevPyr);
		if(pNext)
			gclCutsceneCGTPosPyr(pState->pDef, pList, pNext->vPosition, pNext->vRotation, vNextPos, vNextPyr);

		if(pPrev && pNext) {
			interpPYRByQuat(weight, vPrevPyr, vNextPyr, blendedPyr);
			createMat3YPR(objMat, blendedPyr);
			interpVec3(weight, vPrevPos, vNextPos, objMat[3]);
			tgparams.alpha = interpF32(weight, pPrev->fAlpha, pNext->fAlpha);
		} else if (pPrev) {
			createMat3YPR(objMat, vPrevPyr);
			copyVec3(vPrevPos, objMat[3]);
			tgparams.alpha = pPrev->fAlpha;
		} else if (pNext) {
			createMat3YPR(objMat, vNextPyr);
			copyVec3(vNextPos, objMat[3]);
			tgparams.alpha = pNext->fAlpha;
		} else {
			copyMat4(unitmat, objMat);
			tgparams.alpha = 0.0f;
		}

		tgparams.alpha = CLAMP(tgparams.alpha, 0.0f, 1.0f);
		tgparams.alpha = 1.0f-tgparams.alpha;
		if(tgparams.alpha)
			worldAddTempGroup(pGroupDef, objMat, &tgparams, true);
	}
}

static void gclCutsceneEntityActionStart(CutsceneDef *pDef, Entity *pEnt, CutsceneEntityList *pList, CutsceneEntityPoint *pPoint, bool entCreated)
{
	int i;
	CutsceneEntityActionType actionType = pPoint->actionType;
	Vec3 vPos, vPyr;
	Quat qRot;

	gclCutsceneCGTPosPyr(pDef, pList, pPoint->vPosition, pPoint->vRotation, vPos, vPyr);
	PYRToQuat(vPyr, qRot);

	if(entCreated) {
		if(pList->entityType == CutsceneEntityType_Custom) {
			copyVec3(vPos, pList->vPrevPos);
			copyVec3(vPyr, pList->vPrevRot);
		} else {
			Quat qTempRot;
			PYRToQuat(pList->vPrevRot, qTempRot);
			mmSetPositionFG(pEnt->mm.movement, pList->vPrevPos, __FUNCTION__);
			mmSetRotationFG(pEnt->mm.movement, qTempRot, __FUNCTION__);
		}
	}

	if(actionType == CutsceneEntAction_Waypoint) {
		if(pPoint->common.length == 0) {
			actionType = CutsceneEntAction_Spawn;
		} else if(entCreated && pList->entityType == CutsceneEntityType_Custom){
			actionType = CutsceneEntAction_Spawn;
		}
	}

	switch(actionType)
	{
		xcase CutsceneEntAction_Spawn:
	{
		// it would be better if this did a capsule cast [RMARR - 10/11/11]
		worldSnapPosToGround(entGetPartitionIdx(pEnt), vPos, 2, -2, NULL);
		mmSetPositionFG(pEnt->mm.movement, vPos, __FUNCTION__);
		mmSetRotationFG(pEnt->mm.movement, qRot, __FUNCTION__);
		copyVec3(vPos, pList->vPrevPos);
		copyVec3(vPyr, pList->vPrevRot);
	}
	xcase CutsceneEntAction_Waypoint:
	{
		MovementRequester *mrInteraction = pEnt->mm.mrInteraction;
		F32 fDist = distance3(vPos, pList->vPrevPos);

		if(!mrInteraction && mrInteractionCreate(pEnt->mm.movement, &pEnt->mm.mrInteraction))
			mrInteraction = pEnt->mm.mrInteraction;

		if(fDist && mrInteraction){
			MRInteractionPath *pIntPath = StructAlloc(parse_MRInteractionPath);
			MRInteractionWaypoint *pIntWaypoint = StructAlloc(parse_MRInteractionWaypoint);
			AIAnimList *pAnimList;

			eaPush(&pIntPath->wps, pIntWaypoint);

			copyVec3(vPos, pIntWaypoint->pos);
			copyQuat(qRot, pIntWaypoint->rot);

			pIntWaypoint->seconds = pPoint->common.length;

			mrSurfaceSetSpeed(pEnt->mm.mrSurface, MR_SURFACE_SPEED_FAST, fDist / MAX(pPoint->common.length, 0.001));

			if((pAnimList = GET_REF(pPoint->hAnimList)) && eaSize(&pAnimList->bits))
			{
				pIntWaypoint->flags.forceAnimDuringMove = 1;
				EARRAY_CONST_FOREACH_BEGIN(pAnimList->bits, j, jsize);
					eaiPush(&pIntWaypoint->animBitHandles, mmGetAnimBitHandleByName(pAnimList->bits[j], 0));
				EARRAY_FOREACH_END;
			}

			mrInteractionSetPath(mrInteraction, &pIntPath);
		}
		copyVec3(vPos, pList->vPrevPos);
		copyVec3(vPyr, pList->vPrevRot);
	}
	xcase CutsceneEntAction_AddStance:
	{
		DynSkeleton *pSkeleton = dynSkeletonFromGuid(pEnt->dyn.guidSkeleton);
		if(pSkeleton && pPoint->pchStance) {
			dynSkeletonSetCutsceneStanceWord(pSkeleton, pPoint->pchStance);
		}			
	}
	xcase CutsceneEntAction_PlayFx:
	{
		if(!pPoint->pchFXName){
			printf(	"missing FX name for cutscene\n");
		} else {
			// Add FX
			U32 dfxUID = dtAddFx(pEnt->dyn.guidFxMan, pPoint->pchFXName, NULL, 0, 0, 0.0f, 0, NULL, eDynFxSource_Cutscene, NULL, NULL);
			if(pPoint->bFlashFx) {
				ea32Push(&pPoint->dfxFlashUIDs, dfxUID);
			} else {
				ea32Push(&pPoint->dfxUIDs, dfxUID);
			}			
			ea32Push(&s_pRunningFX, dfxUID);
		}
	}
	xcase CutsceneEntAction_Animation:
	{
		MovementRequester *mrInteraction = pEnt->mm.mrInteraction;
		AIAnimList *pAnimList = GET_REF(pPoint->hAnimList);

		if(!mrInteraction && mrInteractionCreate(pEnt->mm.movement, &pEnt->mm.mrInteraction))
			mrInteraction = pEnt->mm.mrInteraction;

		if(!pAnimList){
			printf(	"Didn't have animlist for cutscene: %s\n",
				REF_STRING_FROM_HANDLE(pPoint->hAnimList));
		} else if(mrInteraction) {
			MRInteractionPath *pIntPath = StructAlloc(parse_MRInteractionPath);
			MRInteractionWaypoint *pIntWaypoint;

			pIntWaypoint = StructAlloc(parse_MRInteractionWaypoint);
			eaPush(&pIntPath->wps, pIntWaypoint);

			copyVec3(pList->vPrevPos, pIntWaypoint->pos);
			PYRToQuat(pList->vPrevRot, pIntWaypoint->rot);

			pIntWaypoint->seconds = pPoint->common.length;

			if(gConf.bNewAnimationSystem){
				if(pAnimList->animKeyword){
					pIntWaypoint->animToStart = mmGetAnimBitHandleByName(pAnimList->animKeyword, 0);
				}
			}else{
				EARRAY_CONST_FOREACH_BEGIN(pAnimList->bits, j, jsize);
				eaiPush(&pIntWaypoint->animBitHandles, mmGetAnimBitHandleByName(pAnimList->bits[j], 0));
				EARRAY_FOREACH_END;
			}

			mrInteractionSetPath(mrInteraction, &pIntPath);

			// Add FX
			for ( i=0; i < eaSize(&pAnimList->FX); i++ ) {
				const char *pcFXName = pAnimList->FX[i];
				U32 dfxUID = dtAddFx(pEnt->dyn.guidFxMan, pcFXName, NULL, 0, 0, 0.0f, 0, NULL, eDynFxSource_Cutscene, NULL, NULL);
				ea32Push(&pPoint->dfxUIDs, dfxUID);
				ea32Push(&s_pRunningFX, dfxUID);
			}
			for ( i=0; i < eaSize(&pAnimList->FlashFX); i++ ) {
				const char *pcFXName = pAnimList->FlashFX[i];
				U32 dfxUID = dtAddFx(pEnt->dyn.guidFxMan, pcFXName, NULL, 0, 0, 0.0f, 0, NULL, eDynFxSource_Cutscene, NULL, NULL);
				ea32Push(&pPoint->dfxFlashUIDs, dfxUID);
				ea32Push(&s_pRunningFX, dfxUID);
			}
		}
	}
	xcase CutsceneEntAction_Despawn:
	{
		for ( i=0; i < eaSize(&s_ppCOEnts); i++ ) {
			if (s_ppCOEnts[i]->entity == pEnt) {
				Entity *pEntParent = entFromEntityRefAnyPartition(s_ppCOEnts[i]->oldEntityRef);
				CutsceneEntityList *pFoundList = NULL;
				if(pEntParent) {
					pEntParent->bInCutscene = false;
					CutsceneSetEntityAlpha(pEntParent, 1);
				}
				gclClientOnlyEntityDestroy(&(s_ppCOEnts[i]));
				eaRemove(&s_ppCOEnts, i);
				break;
			}
		}
	}
	}
}

bool gclCutscenePreLoad(CutsceneDef *pDef)
{
	int i;
	bool entCreated = false;

	for ( i=0; i < eaSize(&pDef->ppEntityLists); i++ ) {
		CutsceneEntityList *pList = pDef->ppEntityLists[i];
		if(	eaSize(&pList->ppEntityPoints) > 0 &&
			pList->ppEntityPoints[0]->common.time == 0 ) {

				Entity *pEnt = entFromEntityRefAnyPartition(pList->entRef);
				if(!pEnt) {
					pEnt = gclCutsceneInitClientEntity(pList);
					pList->entRef = pEnt->myRef;
					if (!pList->bCostumeLoaded)
					{
						pList->bCostumeLoaded = !!GET_REF(pEnt->hWLCostume);
					}				
					pList->iFramesLoaded = 0;
					entCreated = true;

					gclCutsceneEntityActionStart(pDef, pEnt, pList, pList->ppEntityPoints[0], entCreated);
				} 
		}
	}

	return gclCutsceneEntityOncePerFrame(pDef);
}

static void gclCutsceneRunEntity(CutsceneFrameState *pState, CutsceneEntityList *pList, CutsceneEntityPoint *pPoint, F32 elapsedTime, CutsceneRunFunctionAction action)
{
	int i;
	bool entCreated = false;
	Entity *pEnt;

	switch(action) 
	{
		xcase CRFA_Start:
	pEnt = entFromEntityRefAnyPartition(pList->entRef);
	if(!pEnt) {
		pEnt = gclCutsceneInitClientEntity(pList);
		pList->entRef = pEnt->myRef;
		pList->bCostumeLoaded = !!GET_REF(pEnt->hWLCostume);
		pList->iFramesLoaded = 0;
		entCreated = true;
	} 
	gclCutsceneEntityActionStart(pState->pDef, pEnt, pList, pPoint, entCreated);
	xcase CRFA_Mid:
	//Do nothing
	xcase CRFA_End:
	for ( i=0; i < ea32Size(&pPoint->dfxUIDs); i++ ) {
		dtFxKill(pPoint->dfxUIDs[i]);
	}
	ea32Clear(&pPoint->dfxUIDs);
	break;
	}
}

static void gclCutsceneBlendTexture(CutsceneFrameState *pState, CutsceneTextureList *pList, CutsceneTexturePoint *pPrev, CutsceneTexturePoint *pNext, F32 weight)
{
	AtlasTex *pTexture = gclCutsceneLoadTexture(pState, pList);
	if(pTexture) {
		int iWidth, iHeight, iLetterBox;
		U8 iAlpha;
		F32 fScale;
		Vec3 vPos;
		F32 x=0, y=0;

		if(pPrev && pNext) {
			iAlpha = interpF32(weight, pPrev->fAlpha, pNext->fAlpha)*255.0f;
			fScale = interpF32(weight, pPrev->fScale, pNext->fScale);
			interpVec2(weight, pPrev->vPosition, pNext->vPosition, vPos);
		} else if (pPrev) {
			iAlpha = pPrev->fAlpha*255.0f;
			fScale = pPrev->fScale;
			copyVec2(pPrev->vPosition, vPos);
		} else if (pNext) {
			iAlpha = pNext->fAlpha*255.0f;
			fScale = pNext->fScale;
			copyVec2(pNext->vPosition, vPos);
		} else {
			return;
		}
		iAlpha = 255-iAlpha;

		gclCutsceneGetScreenSize(&iWidth, &iHeight, &iLetterBox, FLT_MAX);
		switch(pList->eXAlign) {
			xcase CutsceneAlignX_Left:
		x = vPos[0];
		xcase CutsceneAlignX_Right:
		x = iWidth - vPos[0] - pTexture->width*fScale;
		xcase CutsceneAlignX_Center:
		x = iWidth/2.0f + vPos[0] - pTexture->width*fScale/2.0f;
		}
		switch(pList->eYAlign) {
			xcase CutsceneAlignY_Top:
		y = vPos[1] + iLetterBox;
		xcase CutsceneAlignY_Bottom:
		y = iHeight - vPos[1] - pTexture->height*fScale - iLetterBox;
		xcase CutsceneAlignY_Center:
		y = iHeight/2.0f + vPos[1] - pTexture->height*fScale/2.0f;
		}

		display_sprite(pTexture, x, y, UI_UI2LIB_Z-3.1, fScale, fScale, 0xFFFFFF00 + iAlpha);
	}
}

static void CutscenePlaySound(const char *pcSoundEventPath)
{
	sndPlayAtCharacter(pcSoundEventPath, NULL, -1, NULL, NULL);
}

static void gclCutsceneRunFX(CutsceneFrameState *pState, CutsceneFXList *pList, CutsceneFXPoint *pPoint, F32 elapsedTime, CutsceneRunFunctionAction action)
{
	Vec3 vPos, vPyr;
	gclCutsceneCGTPosPyr(pState->pDef, pList, pPoint->vPosition, zerovec3, vPos, vPyr);

	switch(action) 
	{
		xcase CRFA_Start:
	if(pPoint->pcFXName) {
		pPoint->dfxUID = dtAddFxFromLocation(pPoint->pcFXName, NULL, 0, vPos, NULL, NULL, 0.f, 0, eDynFxSource_Cutscene);
		ea32Push(&s_pRunningFX, pPoint->dfxUID);
	} else {
		ErrorFilenamef(pState->pDef->filename, "No FX specified in cutscene %s at time %f", pState->pDef->name, pPoint->common.time);
	}
	xcase CRFA_Mid:
	//Do nothing
	xcase CRFA_End:
	dtFxKill(pPoint->dfxUID);
	break;
	}
}

static void gclCutsceneRunSound(CutsceneFrameState *pState, CutsceneSoundList *pList, CutsceneSoundPoint *pPoint, F32 elapsedTime, CutsceneRunFunctionAction action)
{
	const char *pSoundPath = gclCutsceneGetSoundPath(pState, pPoint);

	switch(action) 
	{
		xcase CRFA_Start:
			if(elapsedTime-1.0f < pPoint->common.time) {
				int idx = eaFind(&s_ppPlayedSounds, pSoundPath);
				if(idx < 0) {
					if(pPoint->pchCutsceneEntName && pPoint->pchCutsceneEntName[0]) {
						Entity *pEnt = gclCutsceneGetCutsceneEntByName(pState->pDef, pPoint->pchCutsceneEntName);
						if(pEnt) {
							sndPlayFromEntity(pSoundPath, entGetRef(pEnt), pState->pDef->filename, false);
						}
					} else if(pPoint->bUseCamPos) {
						sndPlayAtPosition(pSoundPath, vecParamsXYZ(pState->vFinalPos), pState->pDef->filename, -1, NULL, NULL, false);
					} else {
						Vec3 vPos, vPyr;
						gclCutsceneCGTPosPyr(pState->pDef, pList, pPoint->vPosition, zerovec3, vPos, vPyr);
						sndPlayAtPosition(pSoundPath, vecParamsXYZ(vPos), pState->pDef->filename, -1, NULL, NULL, false);
					}
					if(!pPoint->common.fixedLength)
						eaPush(&s_ppPlayedSounds, pSoundPath);
					eaPushUnique(&s_ppNeedToStopSounds, pSoundPath);
				}
			}
		xcase CRFA_Mid:
			//Do nothing
		xcase CRFA_End:
			if(!pPoint->common.fixedLength) {
				eaFindAndRemove(&s_ppPlayedSounds, pSoundPath);
				sndStopOneShot(pSoundPath);
			}
			eaFindAndRemove(&s_ppNeedToStopSounds, pSoundPath);
	}
}

static TextAttribs* gclCutscene_GetSubtitleAttribs()
{
	static TextAttribs *pSubtitleAttribs = NULL;
	if(!pSubtitleAttribs)	{
		UIStyleFont *pFont = ui_StyleFontGet("Game_HUD");
		pSubtitleAttribs = smf_TextAttribsFromFont(pSubtitleAttribs, pFont);
	}
	return pSubtitleAttribs;
}

void gclCutsceneGetScreenSize(S32 *iWindowWidth, S32 *iWindowHeight, S32 *iLetterBox, F32 elapsedTime)
{
	gfxGetActiveDeviceSize(iWindowWidth, iWindowHeight);
	(*iLetterBox) = max(s_iLetterboxMinHeight, s_fLetterboxWindowHeight * (*iWindowHeight)) * min(elapsedTime / 2, 1);
}

static void gclCutsceneShowSubtitle(SMFBlock *pText, F32 fStart, F32 fEnd, F32 elapsedTime)
{
	F32 fAlpha;
	S32 iTextHeight;
	S32 iSubtitleOffset;
	TextAttribs *pSubtitleAttribs = gclCutscene_GetSubtitleAttribs();
	S32 iWindowHeight, iWindowWidth, iLetterBox;

	if(!pText)
		return;

	iTextHeight = smfblock_GetHeight(pText);
	gclCutsceneGetScreenSize(&iWindowWidth, &iWindowHeight, &iLetterBox, FLT_MAX);
	smf_TextAttribsSetScale(pSubtitleAttribs, min(iWindowWidth / 800.0, iWindowHeight / 600.0));
	iSubtitleOffset = iWindowHeight * 0.023;

	if (fStart && elapsedTime - fStart < s_fCutsceneTextFadeTime)
		fAlpha = (elapsedTime - fStart) / s_fCutsceneTextFadeTime;
	else if (fEnd && fEnd - elapsedTime < s_fCutsceneTextFadeTime)
		fAlpha = (fEnd - elapsedTime) / s_fCutsceneTextFadeTime;
	else
		fAlpha = 1.0f;

	fAlpha *= 255.0f;
	fAlpha = CLAMP(fAlpha, 0, 255);

	smf_ParseAndDisplay(pText, NULL, iWindowWidth*0.05, iWindowHeight - iLetterBox - iTextHeight - iSubtitleOffset, UI_UI2LIB_Z-1, iWindowWidth*0.9, 0, false, false, false, pSubtitleAttribs, fAlpha, NULL);
}

static void gclCutsceneShowChatBubble(CutsceneDef *pDef, const char *pcName, F32 fDuration, const char *pTranslatedMessage)
{
	Entity *pPlayer = NULL;
	ChatUserInfo *pFrom = NULL;
	char *pcFormatted = NULL;
	ChatData *pData = NULL;
	ChatBubbleData *pBubbleData = NULL;
	Entity *pEntity = gclCutsceneGetCutsceneEntByName(pDef, pcName);
	if(!pEntity)
		return;

	pPlayer = gclCutsceneGetPlayer();
	pFrom = ChatCommon_CreateUserInfoFromNameOrHandle(pcName);
	pFrom->nonPlayerEntityRef = pEntity->myRef;
	pData = StructCreate(parse_ChatData);
	pBubbleData = StructCreate(parse_ChatBubbleData);
	pData->pBubbleData = pBubbleData;
	pBubbleData->fDuration = fDuration;
	pBubbleData->pchBubbleStyle = StructAllocString("Default");

	FormatGameString(&pcFormatted, pTranslatedMessage, STRFMT_ENTITY(pPlayer), STRFMT_END);

	ChatLog_AddEntityMessage(pFrom, kChatLogEntryType_NPC, pcFormatted, pData);

	estrDestroy(&pcFormatted);
	StructDestroy(parse_ChatData, pData); // Includes ChatBubbleData
	StructDestroy(parse_ChatUserInfo, pFrom);
}

static void gclCutsceneSendChatMessage(CutsceneDef *pDef, const char *pTranslatedMessage)
{
	Entity *pPlayer = NULL;
	ChatUserInfo *pFrom = NULL;
	char *pcFormatted = NULL;
	ChatData *pData = NULL;

	pPlayer = gclCutsceneGetPlayer();
	pFrom = ChatCommon_CreateUserInfoFromNameOrHandle("");
	pData = StructCreate(parse_ChatData);
	pData->pBubbleData = NULL;

	FormatGameString(&pcFormatted, pTranslatedMessage, STRFMT_ENTITY(pPlayer), STRFMT_END);

	ChatLog_AddEntityMessage(pFrom, kChatLogEntryType_NPC, pcFormatted, pData);

	estrDestroy(&pcFormatted);
	StructDestroy(parse_ChatData, pData);
	StructDestroy(parse_ChatUserInfo, pFrom);
}

static void gclCutsceneRunSubtitle(CutsceneFrameState *pState, CutsceneSubtitleList *pList, CutsceneSubtitlePoint *pPoint, F32 elapsedTime, CutsceneRunFunctionAction action)
{
	F32 fStart = pPoint->common.time;
	F32 fEnd = pPoint->common.time + pPoint->common.length;
	bool isChatBubble = (pPoint->pchCutsceneEntName && pPoint->pchCutsceneEntName[0]);
	const char *pcText = gclCutsceneGetTranslatedMessageForSubtitle(pState, pPoint);

	if(pcText)
	{
		switch(action) 
		{
			xcase CRFA_Start:
				if(isChatBubble) {
					F32 fDuration = pPoint->common.length + (pPoint->common.time - elapsedTime);
					gclCutsceneShowChatBubble(pState->pDef, pPoint->pchCutsceneEntName, fDuration, pcText);
				} else {
					gclCutsceneSendChatMessage(pState->pDef, pcText);
				}
			xcase CRFA_Mid:
				if(!isChatBubble) {
					gclCutsceneInitSubtitleBlock(pState->pDef, &pPoint->pTextBlock, pcText, true);
					gclCutsceneShowSubtitle(pPoint->pTextBlock, fStart, fEnd, elapsedTime);
				}
			xcase CRFA_End:
				//Do nothing
				break;
		}
	}
}

bool gclGetCutsceneCameraPathListPosPyr(CutsceneDef *pDef, F32 elapsedTime, Vec3 cameraPos /*out*/, Vec3 cameraPyr /*out*/, GfxSkyData *sky_data, bool reset, F32 timestep)
{
	int i;
	F32 startTime = 0;
	CutsceneFrameState state = {0};
	bool found = false;
	bool using_dof = false;
	F32 ratio = 1.0f;
	Vec3 startPos, startPyr;
	F32 focusDist=0;

	if(!pDef || !pDef->pPathList)
		return false;

	state.pDef = pDef;
	state.pSkyData = sky_data;
	state.pCamera = gfxGetActiveCameraController();

	if(timestep >= 0)
		ratio = saturate(1.0f - pow(pDef->fBlendRate, timestep*30));

	copyVec3(cameraPos, startPos);
	copyVec3(cameraPyr, startPyr);

	if(firstCutsceneFrame)
		gclCutsceneSetFOV(state.pCamera, gfxGetDefaultFOV());

	if(sky_data != pOldSkyData){
		pOldSkyData = sky_data;
	}

	if(reset)
	{
		prevElapsedTime = (elapsedTime ? elapsedTime+0.001f : -0.001f);
		prevPathIdx = 0;
		copyVec3(cameraPos, prevPos);
		copyVec3(cameraPyr, prevPyr);
		prevPosEaseOut = prevLookAtEaseOut = 1;
	}

	for( i=0; i < eaSize(&pDef->pPathList->ppPaths); i++ )
	{
		CutscenePath *pPath = pDef->pPathList->ppPaths[i];
		F32 pathEndTime = cutscene_GetPathEndTimeUnsafe(pPath);
		if(pathEndTime < 0)
			pathEndTime = startTime;

		//Find where the path is coming from initially (first time is from original cam position)
		if(i > prevPathIdx)
		{
			int size;
			CutscenePath *pPrevPath = pDef->pPathList->ppPaths[i-1];
			prevPathIdx = i;
			size = eaSize(&pPrevPath->ppPositions);
			if(size > 0)
			{
				if(pPrevPath->type == CutscenePathType_ShadowEntity)
					copyVec3(cameraPos, prevPos);
				else
					copyVec3(pPrevPath->ppPositions[size-1]->pos, prevPos);
				prevPosEaseOut = pPrevPath->ppPositions[size-1]->easeOut;
			}
			size = eaSize(&pPrevPath->ppTargets);
			if(size > 0)
			{
				if(pPrevPath->type == CutscenePathType_WatchEntity || pPrevPath->type == CutscenePathType_ShadowEntity)
					copyVec3(cameraPyr, prevPyr);
				else
					gclMakeCutsceneCamPYR(NULL, prevPos, pPrevPath->ppTargets[size-1]->pos, prevPyr);
				prevLookAtEaseOut = pPrevPath->ppTargets[size-1]->easeOut;
			}
		}

		if(elapsedTime <= pathEndTime)
		{
			if(gclGetCutsceneCameraPathPosPyr(&state, pPath, elapsedTime, startTime, cameraPos, cameraPyr))
				found = true;
			break;
		}
		startTime = pathEndTime;
	}

	if (pDef->bAutoAdjustCameraDistance)
	{
		Entity* pEnt = gclCutsceneGetPlayer();
		Vec3 vEntPos, vDir;
		F32 fEntRadius = pEnt ? entGetBoundingSphere(pEnt, vEntPos) : 0.0f;
		if (fEntRadius > pDef->fAutoAdjustMinEntityRadius)
		{
			F32 fRadiusDiff = fEntRadius - pDef->fAutoAdjustMinEntityRadius;
			createMat3_2_YP(vDir, cameraPyr);
			normalVec3(vDir);
			scaleAddVec3(vDir, fRadiusDiff, cameraPos, cameraPos);
		}
	}
	if (pDef->bCameraClippingAvoidance)
	{
		GfxCameraController* pCamera = &gGCLState.pPrimaryDevice->cutscenecamera;
		int iRayCastHits = 0;
		Mat4 xMat;
		Vec3 vEntPos, vDirToEnt;
		F32 fDistance, fCamDist;
		gclCamera_GetTargetPosition(pCamera, vEntPos);
		subVec3(cameraPos, vEntPos, vDirToEnt);
		fDistance = lengthVec3(vDirToEnt);
		orientMat3(xMat, vDirToEnt);
		copyVec3(cameraPos, xMat[3]);
		fCamDist = gclCamera_GetZDistanceFromTargetEx(pCamera, xMat, vEntPos, &iRayCastHits);
		if (iRayCastHits > 0 && fCamDist >= 0.0f && fCamDist < fDistance)
		{
			scaleAddVec3(xMat[2], fCamDist, vEntPos, cameraPos);
		}
	}

	//If bHideAllPlayers is true, the cutscene can define how long it takes for the players to disappear
	if (pDef->bHideAllPlayers){
		bool bValidFadeTime = true;

		//The fade time should not exceed the total cutscene time
		if(pDef->fTimeToFadePlayers > cutscene_GetLength(pDef, true)){
			ErrorFilenamef(pDef->filename, "Time to fade players is %f which is longer than total cutscene length, %f\n", pDef->fTimeToFadePlayers, cutscene_GetLength(pDef, true));
			bValidFadeTime = false;
		}

		//If there is no fade time, the players will just pop out of existence
		if(bValidFadeTime && pDef->fTimeToFadePlayers > 0 && elapsedTime <= pDef->fTimeToFadePlayers){
			gclCutsceneSetAllPlayersAlpha(interpF32(CLAMPF32(elapsedTime / pDef->fTimeToFadePlayers, 0.0f, 1.0f), 1.0f, 0.0f), false);
		} else {
			gclCutsceneSetAllPlayersAlpha(0.0, true);
		}
	}

	//If we are moving too far this frame, just pop to the location.
	if(distance3Squared(startPos, cameraPos) > 1000000.0f)
		ratio = 1.0f;
	interpVec3(ratio, startPos, cameraPos, cameraPos);
	interpPYRByQuat(ratio, startPyr, cameraPyr, cameraPyr);
	copyVec3(cameraPos, state.vFinalPos);
	copyVec3(cameraPyr, state.vFinalPyr);

	// CutsceneEffectsAndEvents
	// Add a call to apply points with a callback function that will set the values

	if(gclCutsceneApplyPoints(&state, pDef->pFadeList, prevElapsedTime, elapsedTime, gclCutsceneBlendFade, NULL))
		found = true;

	if(gclCutsceneApplyPoints(&state, pDef->pDOFList, prevElapsedTime, elapsedTime, gclCutsceneBlendDOF, NULL))
		found = true;

	if(gclCutsceneApplyPoints(&state, pDef->pFOVList, prevElapsedTime, elapsedTime, gclCutsceneBlendFOV, NULL))
		found = true;

	if(gclCutsceneApplyPoints(&state, pDef->pShakeList, prevElapsedTime, elapsedTime, NULL, gclCutsceneRunCameraShake))
		found = true;

	if(gclCutsceneApplyListsPoints(&state, pDef->ppObjectLists, prevElapsedTime, elapsedTime, gclCutsceneBlendObject, NULL))
		found = true;

	if(gclCutsceneApplyListsPoints(&state, pDef->ppEntityLists, prevElapsedTime, elapsedTime, NULL, gclCutsceneRunEntity))
		found = true;
	gclCutsceneEntityOncePerFrame(pDef);

	if(gclCutsceneApplyListsPoints(&state, pDef->ppTexLists, prevElapsedTime, elapsedTime, gclCutsceneBlendTexture, NULL))
		found = true;

	if(gclCutsceneApplyListsPoints(&state, pDef->ppFXLists, prevElapsedTime, elapsedTime, NULL, gclCutsceneRunFX))
		found = true;

	if(gclCutsceneApplyListsPoints(&state, pDef->ppSoundLists, prevElapsedTime, elapsedTime, NULL, gclCutsceneRunSound))
		found = true;

	if(gclCutsceneApplyListsPoints(&state, pDef->ppSubtitleLists, prevElapsedTime, elapsedTime, NULL, gclCutsceneRunSubtitle))
		found = true;

	if(gclCutsceneApplyPoints(&state, pDef->pUIGenList, prevElapsedTime, elapsedTime, NULL, gclCutsceneRunUIGen))
		found = true;

	prevElapsedTime = elapsedTime;
	firstCutsceneFrame = false;
	return found;
}

// Returns false if we've run out of targets.  Sets target and weight
static bool gclFindCurrentTarget(CutscenePos*** targets, /* out */ int* curTargIndex, InterpolationType defaultInterp, F32* weight)
{
	int curTarget;
	int maxTargets = eaSize(targets);
	F32 curTime = 0;
	F32 tempWeight;
	InterpolationType interpType = defaultInterp;

	// Find the two positions we're interpolating between
	for(curTarget=0; curTarget < maxTargets; curTarget++)
	{
		CutscenePos* target = (*targets)[curTarget];

		if(target->fMoveTime && g_ClientCutscene->elapsedTime < curTime + target->fMoveTime)
		{
			*weight = (g_ClientCutscene->elapsedTime - curTime) / target->fMoveTime;
			if(target->eInterpolate != InterpolationType_Default)
				interpType = target->eInterpolate;
			break;
		}
		curTime += target->fMoveTime;

		if(g_ClientCutscene->elapsedTime < curTime + target->fHoldTime)
		{
			*weight = 1.0;
			break;
		}
		curTime += target->fHoldTime;
	}

	if(curTarget >= maxTargets)
	{
		*curTargIndex = maxTargets -1;
		*weight = 1.0;
		return false;
	}

	switch(interpType)
	{
	case InterpolationType_Default:
	case InterpolationType_Smooth:
		// Weight is -2w^3 + 3w^2, for a smoother feel
		tempWeight = *weight;
		tempWeight = -2 * tempWeight * tempWeight * tempWeight + 3 * tempWeight * tempWeight;
		*weight = tempWeight;
		break;

	case InterpolationType_SpeedUp:
		// Weight is w^2
		*weight = *weight * *weight;
		break;
	case InterpolationType_SlowDown:
		// Weight is 1 - w^2
		*weight = 1 - (*weight * *weight);
		break;
	}

	*weight = CLAMP(*weight, 0, 1);

	*curTargIndex = curTarget;
	return true;
}

static bool gclCutsceneBasicInitStartPos(CutsceneDef *pDef)
{
	F32 weight = 0;
	int curPosition = 0;
	int curTarget = 0;
	bool foundPos = false;
	bool foundTarget = false;

	if(pDef->ppCamPositions)
	{
		// Find the position where the cutscene should be right now
		foundPos = gclFindCurrentTarget(&pDef->ppCamPositions, &curPosition, pDef->eInterpolate, &weight);
		prevPosIdx = curPosition;

		// Set the cutscene's "previous position" to the previous cutscene point
		if(curPosition > 0){
			copyVec3(pDef->ppCamPositions[curPosition-1]->vPos, prevPos);
		}
	}

	// Repeat for the camera's target to get its PYR
	if(pDef->ppCamTargets)
	{
		Vec3 lookDirection;
		foundTarget = gclFindCurrentTarget(&pDef->ppCamTargets, &curTarget, pDef->eInterpolate, &weight);
		prevTargIdx = curTarget;

		if(curTarget > 0)
		{
			// These PYRs are actually backwards because the camera looks along the negative Z axis
			subVec3(prevPos, pDef->ppCamTargets[curTarget-1]->vPos, lookDirection);

			// Avoid divide-by-zero
			if(!vec3IsZero(lookDirection)){
				orientYPR(prevPyr, lookDirection);
			}
		}
	}

	return foundPos || foundTarget;
}

bool gclGetCutsceneCameraBasicPosPyr(CutsceneDef *pDef, Vec3 cameraPos /*out*/, Vec3 cameraPYR /*out*/)
{
	Vec3 secondPos;
	Vec3 secondPyr;
	F32 weight = 0;
	int curPosition = 0;
	int curTarget = 0;
	bool foundPos = false;
	bool foundTarget = false;

	// Interpolate the camera's position
	if(pDef->ppCamPositions)
	{
		foundPos = gclFindCurrentTarget(&pDef->ppCamPositions, &curPosition, pDef->eInterpolate, &weight);

		if(curPosition > prevPosIdx)
		{
			prevPosIdx = curPosition;
			copyVec3(cameraPos, prevPos);
		}

		copyVec3(pDef->ppCamPositions[curPosition]->vPos, secondPos);
		interpVec3(weight, prevPos, secondPos, cameraPos);
	}

	// Repeat for the camera's target to get its PYR
	if(pDef->ppCamTargets)
	{
		Vec3 lookDirection;
		foundTarget = gclFindCurrentTarget(&pDef->ppCamTargets, &curTarget, pDef->eInterpolate, &weight);

		if(curTarget > prevTargIdx)
		{
			prevTargIdx = curTarget;
			copyVec3(cameraPYR, prevPyr);
		}

		// These PYRs are actually backwards because the camera looks along the negative Z axis
		subVec3(cameraPos, pDef->ppCamTargets[curTarget]->vPos, lookDirection);

		// Avoid divide-by-zero
		if(!vec3IsZero(lookDirection))
		{
			orientYPR(secondPyr, lookDirection);
			interpPYR(weight, prevPyr, secondPyr, cameraPYR);
		}
	}

	return foundPos || foundTarget;
}

static void gclCutscene_UpdateSubtitles(SMFBlock *pSubtitleBlock, F32 elapsedTime)
{
	S32 i;
	S32 iX, iY;
	S32 iHeight = 0;
	F32 fStart, fEnd;
	TextAttribs *pSubtitleAttribs = gclCutscene_GetSubtitleAttribs();
	static TextAttribs *s_pAttribs;
	SMBlock *pBlock = NULL;
	S32 iWindowHeight, iWindowWidth, iLetterBox;
	gclCutsceneGetScreenSize(&iWindowWidth, &iWindowHeight, &iLetterBox, elapsedTime);
	iY = iWindowHeight - iLetterBox;

	smf_TextAttribsSetScale(pSubtitleAttribs, min(iWindowWidth / 800.0, iWindowHeight / 600.0));
	smf_ParseAndFormat(pSubtitleBlock, NULL, 0, 0, 0, iWindowWidth * 0.7, 0, false, false, false, pSubtitleAttribs);

	// s_pSubtitleAttribs is the "external" view, where the earrays are not actually earrays.
	// s_pAttribs is the "internal" view, where we've transformed the defaults into a form
	// suitable rendering/parsing. Normally smf_ParseAndDisplay does this for us, but we're
	// not using it because we want to exclude some blocks.
	s_pAttribs = InitTextAttribs(s_pAttribs, pSubtitleAttribs);

	iX = (iWindowWidth - smfblock_GetWidth(pSubtitleBlock)) / 2;

	for (i = 0; i < eaSize(&pSubtitleBlock->pBlock->ppBlocks); i++)
	{
		pBlock = pSubtitleBlock->pBlock->ppBlocks[i];
		if (smf_GetTime(pBlock, &fStart, &fEnd)
			&& (elapsedTime > fStart && elapsedTime < fEnd))
			iHeight += max(pBlock->pos.iHeight, pBlock->pos.iMinHeight);
	}

	iY -= iHeight;
	for (i = 0; i < eaSize(&pSubtitleBlock->pBlock->ppBlocks); i++)
	{
		pBlock = pSubtitleBlock->pBlock->ppBlocks[i];
		if (smf_GetTime(pBlock, &fStart, &fEnd)
			&& (elapsedTime > fStart && elapsedTime < fEnd))
		{
			F32 fAlpha;
			if (fStart && elapsedTime - fStart < s_fCutsceneTextFadeTime)
				fAlpha = (elapsedTime - fStart) / s_fCutsceneTextFadeTime;
			else if (fEnd && fEnd - elapsedTime < s_fCutsceneTextFadeTime)
				fAlpha = (fEnd - elapsedTime) / s_fCutsceneTextFadeTime;
			else
				fAlpha = 1;
			// We're setting the Y offset manually, so correct the calculated one.
			smf_Render(pBlock, s_pAttribs, iX, iY - pBlock->pos.iY, UI_UI2LIB_Z-1, CLAMP(fAlpha * 255, 0, 255), 1.0f, NULL, NULL);
			iY += max(pBlock->pos.iHeight, pBlock->pos.iMinHeight);
		}
	}
}

static void gclCutscene_UpdateTransitionText(S32 iWindowWidth, S32 iWindowHeight, S32 iLetterBox)
{
	S32 iX, iY;
	F32 fAlpha, fStartTime, fEndTime;
	F32 fFadeTime = s_TransitionTextDef.fFadeTime;
	SMBlock *pBlock = eaGet(&s_pTransTextBlock->pBlock->ppBlocks, 0);

	if ( pBlock && smf_GetTime(pBlock, &fStartTime, &fEndTime) )
	{
		smf_TextAttribsSetScale(s_pTransTextAttribs, s_TransitionTextDef.fScale);

		iX = iWindowWidth * s_TransitionTextDef.fPercentX;
		iY = iLetterBox + iWindowHeight * s_TransitionTextDef.fPercentY;

		if (fStartTime && g_ClientCutscene->elapsedTime - fStartTime < fFadeTime)
			fAlpha = (g_ClientCutscene->elapsedTime - fStartTime) / fFadeTime;
		else if (fEndTime && fEndTime - g_ClientCutscene->elapsedTime < fFadeTime)
			fAlpha = (fEndTime - g_ClientCutscene->elapsedTime) / fFadeTime;
		else
			fAlpha = 1.0f;

		smf_ParseAndDisplay(s_pTransTextBlock, NULL, iX, iY - pBlock->pos.iY, 1, iWindowWidth * 0.7f, 0, false, false, false, s_pTransTextAttribs, CLAMP(fAlpha * 255, 0, 255), NULL);
	}
}

static void gclCutsceneInitSubtitleBlock(CutsceneDef *pDef, SMFBlock **pSubtitleBlock, const char *pTranslatedMessage, bool bCenter)
{
	S32 iWindowWidth, iWindowHeight;
	if (!(*pSubtitleBlock)) {
		if (pTranslatedMessage) {
			TextAttribs *pSubtitleAttribs = gclCutscene_GetSubtitleAttribs();
			char *pcFormated = NULL;
			char *pcCentered = NULL;
			Entity *pPlayer = gclCutsceneGetPlayer();

			FormatGameString(&pcFormated, pTranslatedMessage, STRFMT_ENTITY(pPlayer), STRFMT_END);

			gfxGetActiveDeviceSize(&iWindowWidth, &iWindowHeight);
			estrPrintf(&pcCentered, "<span align=center>%s<span>", pcFormated);

			(*pSubtitleBlock) = smfblock_Create();
			smf_ParseAndFormat((*pSubtitleBlock), (bCenter ? pcCentered : pcFormated), 0, 0, 0, iWindowWidth * 0.7, 0, false, false, false, pSubtitleAttribs);

			estrDestroy(&pcFormated);
			estrDestroy(&pcCentered);
		}
	}
}

static void gclCutsceneInitTransitionTextBlock(DoorTransitionType eTransitionType)
{
	S32 iWindowWidth, iWindowHeight;
	const char* pchTransitionText = ui_GetCurrentMapTransitionText();

	gfxGetActiveDeviceSize(&iWindowWidth, &iWindowHeight);

	if (eTransitionType == kDoorTransitionType_Arrival && !s_pTransTextBlock && pchTransitionText && pchTransitionText[0])
	{
		char pchBuffer[1024];
		F32 fStart = s_TransitionTextDef.fStartTime;
		F32 fEnd = s_TransitionTextDef.fEndTime;
		sprintf(pchBuffer, "<time start=%f end=%f>\n%s\n</time>", fStart, fEnd, pchTransitionText);

		s_pTransTextBlock = smfblock_Create();
		smf_ParseAndFormat(s_pTransTextBlock, pchBuffer, 0, 0, 0, iWindowWidth * 0.7, 0, false, false, false, s_pTransTextAttribs);
	}
}

static void gclCutsceneOncePerFrame(void)
{
	CutsceneDef *pDef = g_ClientCutscene->pDef;
	S32 iWindowHeight, iWindowWidth, iLetterBox;
	const char *pcText = pDef->pcTranslatedSubtitles;
	if(!pcText || !pcText[0])
		pcText = TranslateDisplayMessageOrEditCopy(pDef->Subtitles);

	gclCutsceneGetScreenSize(&iWindowWidth, &iWindowHeight, &iLetterBox, g_ClientCutscene->elapsedTime);

	if (iLetterBox)
	{
		display_sprite(white_tex_atlas, 0, 0, UI_UI2LIB_Z-2, (F32)iWindowWidth / white_tex_atlas->width, (F32)iLetterBox / white_tex_atlas->height, 0xFF);
		display_sprite(white_tex_atlas, 0, iWindowHeight - iLetterBox, UI_UI2LIB_Z-2, (F32)iWindowWidth / white_tex_atlas->width, (F32)iLetterBox / white_tex_atlas->height, 0xFF);
	}

	if(pcText)
		gclCutsceneInitSubtitleBlock(pDef, &s_pSubtitleBlock, pcText, false);

	if (	s_pTransTextBlock
		&&	g_ClientCutscene->elapsedTime >= s_TransitionTextDef.fStartTime
		&&	g_ClientCutscene->elapsedTime < s_TransitionTextDef.fEndTime )
	{
		gclCutscene_UpdateTransitionText(iWindowWidth, iWindowHeight, iLetterBox);
	}
	else if (s_pSubtitleBlock)
	{
		gclCutscene_UpdateSubtitles(s_pSubtitleBlock, g_ClientCutscene->elapsedTime);
	}

	if (s_bCutsceneShowTime)
	{
		gfxfont_SetFontEx(&g_font_Mono, false, false, 1, false, 0xFFFFFFFF, 0xFFFFFFFF);
		gfxfont_Printf(10, 100, GRAPHICSLIB_Z, 3, 3, 0, "%g", g_ClientCutscene->elapsedTime);
	}
}

bool gclCutsceneIsSet(void)
{
	return g_ClientCutscene && g_ClientCutscene->pDef;
}

bool gclCutsceneAnimListsAreLoaded(F32 timestep)
{
	//This is used to make sure that all of the AIAnimLists used by a cutscene have made it from the server to the client
	//Hopefully, people will check this before running a cutscene, otherwise it's not guaranteed to play any of the animation in it
	CutsceneDef *pDef;
	S32 i, j;

	//check if the anims have already been loaded or if there's a timeout
	s_fCutsceneAnimLoadWaitTime -= timestep;
	if (s_bCutsceneAnimsLoaded ||
		s_fCutsceneAnimLoadWaitTime < 0.f)
	{
		//printfColor(COLOR_RED,"AnimLists loaded or timed out\n");
		return true;
	}

	// opt out if we find missing data
	if (pDef = SAFE_MEMBER(g_ClientCutscene, pDef))
	{
		for (i = 0; i < eaSize(&pDef->ppEntityLists); i++)
		{
			for (j = 0; j < eaSize(&pDef->ppEntityLists[i]->ppEntityPoints); j++)
			{
				if (REF_IS_SET_BUT_ABSENT(pDef->ppEntityLists[i]->ppEntityPoints[j]->hAnimList))
				{
					//found an AIAnimList that hasn't made it from the server to the client yet
					//printfColor(COLOR_RED,"AnimLists NOT loaded!\n");
					return false;
				}
			}
		}
	}

	//either there are no AnimLists or all of them are loaded
	s_bCutsceneAnimsLoaded = true;
	return true;
}

bool gclGetCutsceneCameraPosPyr(F32 timestep, Vec3 cameraPos /*out*/, Vec3 cameraPYR /*out*/, GfxSkyData *sky_data)
{
	if(!g_ClientCutscene || !g_ClientCutscene->pDef)
		return false;

	if (s_fCutscenePauseThenEndTime < FLT_EPSILON)
	{
		g_ClientCutscene->elapsedTime += timestep;
	}
	else
	{
		s_fCutscenePauseThenEndTime -= timestep;
		if (s_fCutscenePauseThenEndTime < FLT_EPSILON)
		{
			return false;
		}
	}

	gclCutsceneOncePerFrame();

	if(g_ClientCutscene->pDef->pPathList){
		return gclGetCutsceneCameraPathListPosPyr(g_ClientCutscene->pDef, g_ClientCutscene->elapsedTime, cameraPos, cameraPYR, sky_data, false, timestep);
	}
	return gclGetCutsceneCameraBasicPosPyr(g_ClientCutscene->pDef, cameraPos, cameraPYR);
}

// ----------------------------------------------------------------------------------
// Cutscene client commands for the server to control which cutscene is playing
// ----------------------------------------------------------------------------------



static void CutscenePlay(F32 fStartTime)
{
	if(g_ClientCutscene->pDef->pchCutsceneSound)
	{
		// TODO - pass fStartTime into here
		// For now, don't play the sound if we're more than 1 second into the cutscene (since it won't sync correctly)
		if (fStartTime < 1.f){
			sndPlayAtCharacter(g_ClientCutscene->pDef->pchCutsceneSound, g_ClientCutscene->pDef->filename, -1, NULL, NULL);
		}
	}

	if(g_ClientCutscene->pDef->pCutsceneDepthOfField)
	{
		CutsceneDOF* pCutDOF = g_ClientCutscene->pDef->pCutsceneDepthOfField;

		gfxSkySetCustomDOF(gfxGetActiveCameraView()->sky_data, pCutDOF->nearDist, pCutDOF->nearValue, pCutDOF->focusDist, pCutDOF->focusValue,
			pCutDOF->farDist, pCutDOF->farValue, pCutDOF->fade_in, pCutDOF->fade_in_rate);
	}

	if (g_ClientCutscene->pDef->bDisableCamLight) {
		gfxEnableCameraLight(false);
	}
}

AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0);
void CutsceneStartOnClient(F32 fStartTime, DoorTransitionType eTransitionType, CutsceneWorldVars *pUpdatedCutsceneVars)
{
	Entity *pEnt = gclCutsceneGetPlayer();
	Player* pPlayer = pEnt ? entGetPlayer(pEnt) : NULL;

	smf_setPlaySoundCallbackFunc(CutscenePlaySound);

	if(!pPlayer || !pPlayer->pCutscene || emIsEditorActive() || gbNoGraphics)
		return;

	// Automatically exit persistent stances for cutscenes
	if (pEnt->pChar && pEnt->pChar->pPowerRefPersistStance && !pEnt->pChar->bPersistStanceInactive)
	{
		character_EnterPersistStance(PARTITION_CLIENT, pEnt->pChar, NULL, NULL, NULL, pmTimestamp(0), 0, false);
	}
	// Tell the contact system to clean up if it's already playing a cutscene
	contactui_CleanUpCurrentCutscene(NULL);
	contactui_ResetToGameCamera();

	s_eDoorTransitionType = eTransitionType;

	if (!s_pTransTextBlock)
	{
		UIStyleFont *pFont = ui_StyleFontGet(s_TransitionTextDef.pchFont);
		s_pTransTextAttribs = smf_TextAttribsFromFont(s_pTransTextAttribs, pFont);
	}

	smfblock_Destroy(s_pSubtitleBlock);
	s_pSubtitleBlock = NULL;

	// TODO: load from player, don't store statically
	gclCutsceneLoadDef(pPlayer->pCutscene,pUpdatedCutsceneVars);

	if (eTransitionType == kDoorTransitionType_Departure && g_ClientCutscene->pDef->pPathList)
	{
		//if this is a map transfer cutscene, add a path at the end that lasts 5 seconds
		S32 i;
		F32 fAllPathsEndTime = 0.0f;
		CutscenePath* pPath = StructCreate(parse_CutscenePath);
		pPath->type = CutscenePathType_ShadowEntity;
		for (i = eaSize(&g_ClientCutscene->pDef->pPathList->ppPaths)-1; i >= 0; i--)
		{
			F32 fEndTime = cutscene_GetPathEndTimeUnsafe(g_ClientCutscene->pDef->pPathList->ppPaths[i]);
			MAX1F(fAllPathsEndTime, fEndTime);
		}

		{
			CutscenePathPoint* pPoint;
			CutscenePathPoint* pTarget;
			pPoint = StructCreate( parse_CutscenePathPoint );
			pTarget = StructCreate( parse_CutscenePathPoint );
			pPoint->common.time = fAllPathsEndTime;
			pTarget->common.time = fAllPathsEndTime;
			pPoint->common.length = 5;
			pTarget->common.length = 5;
			eaPush(&pPath->ppPositions,pPoint);
			eaPush(&pPath->ppTargets,pTarget);
		}
		eaPush(&g_ClientCutscene->pDef->pPathList->ppPaths,pPath);
	}

	gclCutsceneInitTransitionTextBlock(eTransitionType);

	// Setup the camera itself
	if (gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->cutscenecamera)
		return;

	gGCLState.pPrimaryDevice->activecamera = &gGCLState.pPrimaryDevice->cutscenecamera;
	gGCLState.bLockPlayerAndCamera = true;
	gGCLState.bCutsceneActive = true;
	mouseLock(0);	// WOLF[28Aug12] We need to do this so that alt-tabbing out of a cutscene when the mouse IS locked does not cause problems.
					//   Locking the mouse is entangled in the camera update code in a way that makes it lock the mouse unless we are in a
					//   modal ui. Eventually we might want to change this. 
	
	// TODO: Use movement manager to also lock down player movement via other methods.

	globCmdParse("-ShowGameUI");
	if(!demo_playingBack())
	{
		UIGen *pCutsceneRoot = ui_GenFind("Cutscene_Root", kUIGenTypeNone);
		ui_AddActiveFamilies(UI_FAMILY_CUTSCENE);
		if (pCutsceneRoot)
			ui_GenSendMessage(pCutsceneRoot, "Start");
	}

	gfxCameraControllerCopyPosPyr(&gGCLState.pPrimaryDevice->gamecamera, &gGCLState.pPrimaryDevice->cutscenecamera);
	gclCutsceneStart(gGCLState.pPrimaryDevice->cutscenecamera.camcenter, gGCLState.pPrimaryDevice->cutscenecamera.campyr, fStartTime);

	keybind_PushProfileName("Cutscene");

	// CutscenePlay is what should happen when the cutscene begins; this distinction was important when cutscenes
	// had loading screens (so the camera would change, but there'd be a pause before the cutscene actually began).
	CutscenePlay(fStartTime);
}
