/***************************************************************************
*     Copyright (c) 2006, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#include "contactui_eval.h"
#include "CostumeCommonEntity.h"
#include "CostumeCommonGenerate.h"
#include "expression.h"
#include "contact_common.h"
#include "UIGen.h"
#include "mapstate_common.h"
#include "mission_common.h"
#include "mission_enums.h"
#include "nemesis_common.h"
#include "rewardCommon.h"
#include "soundLib.h"
#include "dynSequencer.h"
#include "storeCommon.h"
#include "GraphicsLib.h"
#include "WLCostume.h"
#include "GameStringFormat.h"
#include "Character.h"
#include "gclEntity.h"
#include "gclMapState.h"
#include "estring.h"
#include "StringCache.h"
#include "StringUtil.h"
#include "Player.h"
#include "SavedPetCommon.h"
#include "EntitySavedData.h"
#include "RegionRules.h"
#include "dynAnimInterface.h"
#include "dynMove.h"
#include "dynSkeleton.h"
#include "GfxCamera.h"
#include "GameClientLib.h"
#include "gclCamera.h"
#include "inputMouse.h"
#include "CharacterAttribs.h"
#include "EntityIterator.h"
#include "dynDraw.h"
#include "gclCommandParse.h"
#include "EntityLib.h"
#include "gclUIGen.h"
#include "gclCamera.h"
#include "gclCutscene.h"
#include "cutscene_common.h"
#include "gclUGC.h"
#include "missionui_eval.h"
#include "NotifyCommon.h"
#include "wininclude.h"
#include "GfxLoadScreens.h"
#include "wlTime.h"
#include "GroupProjectCommon.h"
#include "Guild.h"
#include "WorldGrid.h"
#include "gclGoldenPath.h"
#include "gclUIGenMap.h"


#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "AutoGen/contact_common_h_ast.h"
#include "contactui_eval_h_ast.h"
#include "contactui_eval_c_ast.h"
#include "mission_enums_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

AUTO_ENUM;
typedef enum RemoteContactDisplayRowType
{
	RemoteContactDisplayRowType_Normal,
	RemoteContactDisplayRowType_UGC,
	RemoteContactDisplayRowType_Header,
} RemoteContactDisplayRowType;

AUTO_STRUCT;
typedef struct RemoteContactDisplayRow
{
	const char* pchContactName;		AST(POOL_STRING)
	char* pchOptionKey;
	char* pchHeaderDisplayString;
	bool bIsHeader;
} RemoteContactDisplayRow;

// Most of the message keys are handled by the UI Gens, but these keys are used as defaults
// if the contact doesn't have an override
#define DEFAULT_OFFER_ACCEPT_KEY "ContactUI.Accept"
#define DEFAULT_OFFER_DECLINE_KEY "ContactUI.Decline"
#define REMOTE_CONTACTS_WINDOW_GEN "RemoteContacts_Window"
#define MAX_CONTACT_LOG_ENTRIES 150

#define CONTACT_DIALOG_SIMPLE_GETTER(pEntity, Field, DefaultValue) \
								{ \
									ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog); \
									return pContactDialog ? pContactDialog->Field : (DefaultValue); \
								}
#define CONTACT_DIALOG_ENUM_STR_GETTER(pEntity, EnumType, Field, DefaultString) \
								{ \
									ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog); \
									const char *pchResult = pContactDialog ? StaticDefineIntRevLookup(EnumType##Enum, pContactDialog->Field) : NULL; \
									if (0) { EnumType* pEnumType = &pContactDialog->Field; } \
									return pchResult ? pchResult : (DefaultString); \
								}

RemoteContact** geaCachedRemoteContacts = NULL;
static bool sbRemoteContactsViewed = false;
static int siNodeCount = 0;
static ContactLogEntry** seaContactLog = NULL;
static ContainerID suiContactLogEnt = 0;
static char *s_pchLastDialogChoiceKey = NULL;
static bool s_bSpokesmanWillChange = false;

bool exprGenContactDialogMustShowSingleContinueOption(SA_PARAM_OP_VALID Entity *pEntity);

AUTO_RUN;
void contactUIInit(void)
{
	ui_GenInitStaticDefineVars(ContactTypeEnum, "ContactType");
	ui_GenInitStaticDefineVars(ContactIndicatorEnum, "ContactIndicator");
	ui_GenInitStaticDefineVars(ContactScreenTypeEnum, "ContactScreenType");
	ui_GenInitStaticDefineVars(SkillTypeEnum, "SkillType");
	ui_GenInitStaticDefineVars(NemesisStateEnum, "NemesisState");
	ui_GenInitStaticDefineVars(MissionCreditTypeEnum, "MissionCreditType");
	ui_GenInitStaticDefineVars(MissionLockoutTypeEnum, "MissionLockoutType");

	ui_GenInitStaticDefineVars(LogoutTimerTypeEnum, "LogoutTimerType_");

	ui_GenInitIntVar("ContactDialogState_ViewCompleteMission", ContactDialogState_ViewCompleteMission);
}



AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetIsContact");
bool exprEntGetIsContact(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pEntity)
{
	return gclEntGetIsContact(pPlayerEnt, pEntity);
}

// Gets the contact indicator to display above a given entity for the given player
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetContactIndicator");
const char* exprEntGetContactIndicator(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactInfo* contactInfo = gclEntGetContactInfoForPlayer(pPlayerEnt, pEntity);
	const char* ret = "";

	if (contactInfo)
	{
		ret = StaticDefineIntRevLookup(ContactIndicatorEnum, contactInfo->currIndicator);
	}
	else
	{
		CritterInteractInfo* critterInfo = gclEntGetInteractableCritterInfo(pPlayerEnt, pEntity);

		if (critterInfo && critterInfo->currIndicator != ContactIndicator_NoInfo)
		{
			ret = StaticDefineIntRevLookup(ContactIndicatorEnum, critterInfo->currIndicator);
		}
	}

	return ret ? ret : "";
}

// Gets the contact indicator to display above a given entity for the given player
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetNextMissionOfferLevel");
int exprEntGetNextMissionOfferLevel(SA_PARAM_OP_VALID Entity *pPlayerEnt, SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactInfo* contactInfo = gclEntGetContactInfoForPlayer(pPlayerEnt, pEntity);

	if (contactInfo)
	{
		return contactInfo->nextOfferLevel;
	}
	return 0;
}

// Returns the remotely accessing flag of the current Contact interaction, defined in contact_common.h
// Returns false if there is no current contact dialog
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("EntGetContactDialogRemotelyAccessing");
S32 exprEntGetContactDialogRemotelyAccessing(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, bRemotelyAccessing, false);
}

// Contact UI expressions

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FadeInSoundType");
void exprClientFadeInSoundType(SA_PARAM_NN_STR const char* soundType)
{
	SoundType type = StaticDefineIntGetInt(SoundTypeEnum, soundType);
	sndFadeInType(type);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("FadeOutSoundType");
void exprClientFadeOutSoundType(SA_PARAM_NN_STR const char* soundType)
{
	SoundType type = StaticDefineIntGetInt(SoundTypeEnum, soundType);
	sndFadeOutType(type);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayMusic");
void exprClientPlayMusic(SA_PARAM_OP_STR const char* soundName)
{
	if(soundName && soundName[0])
		sndMusicPlayUI(soundName, "contactui_eval.c");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ClearMusic");
void exprClientClearMusic(void)
{
	sndMusicClearUI();
}

// Camera controller for the camera that focuses on contact's head
static bool s_bIsContactHeadCameraInUse;
static Mat4 s_matContactCamera;

// Toggles the visibility of players around an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenToggleVisibilityOfPlayers);
void contactUI_ToggleVisibilityOfPlayers(bool bVisible)
{
	EntityIterator* iter = entGetIteratorSingleTypeAllPartitions(0, 0, GLOBALTYPE_ENTITYPLAYER);
	Entity *pPlayerEnt;

	while ((pPlayerEnt = EntityIteratorGetNext(iter)))
	{
		if (pPlayerEnt && pPlayerEnt->pPlayer)
		{
			S32 i;
			Entity *pPetEnt;

			if (pPlayerEnt->pSaved)
			{
				// Toggle visibility of pets as well
				for (i = 0; i < eaSize(&pPlayerEnt->pSaved->ppOwnedContainers); i++)
				{
					pPetEnt = entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYSAVEDPET, pPlayerEnt->pSaved->ppOwnedContainers[i]->conID);
					if (pPetEnt)
					{
						pPetEnt->fAlpha = bVisible ? 1.f : 0.f;
						pPetEnt->bForceFadeOut = !bVisible;
					}
				}
			}
		}
	}
	EntityIteratorRelease(iter);
}

// Toggles the visibility of players around an entity
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GenToggleVisibilityOfPlayersIfHeadshotEntExists);
void contactUI_ToggleVisibilityOfPlayersIfHeadshotEntExists(SA_PARAM_OP_VALID Entity *pEntity, bool bVisible)
{
	InteractInfo* pInfo;

	if (pEntity == NULL)
	{
		return;
	}

	// Get the interaction info
	pInfo = SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo);

	if (pInfo &&
		pInfo->pContactDialog &&
		entFromEntityRefAnyPartition(pInfo->pContactDialog->headshotEnt))
	{
		contactUI_ToggleVisibilityOfPlayers(bVisible);
	}
}

// Resets the camera back to the game camera
void contactui_ResetToGameCamera(void)
{
	if (s_bIsContactHeadCameraInUse)
	{
		// Unlock player and the camera
		gGCLState.bLockPlayerAndCamera = false;

		// Set the camera back to the game camera
		gclSetGameCameraActive();

		s_bIsContactHeadCameraInUse = false;
	}
}

static void VecOffsetInFacingDirection(Vec3 vec, Vec3 vecPY, F32 fFront, F32 fRight, F32 fAbove)
{
	Vec3 vecDir;
	F32 fAngle;

	createMat3_2_YP(vecDir, vecPY);

	vecDir[1] = 0;
	normalVec3(vecDir);
	scaleAddVec3XZ(vecDir,fFront,vec,vec);

	fAngle = atan2(-vecDir[2],vecDir[0]);
	fAngle += HALFPI;
	vecDir[0] = cos(fAngle);
	vecDir[2] = -sin(fAngle);
	scaleAddVec3XZ(vecDir,fRight,vec,vec);

	vec[1] += fAbove;
}

// The number of seconds elapsed since the current cut-scene started
static F32 s_fCurrentCutSceneElapsedTime = 0.f;

// Current cut-scene def
static CutsceneDef *s_pCurrentCutScene = NULL;

// Team spokesman ID for the current cut-scene
static ContainerID s_iCutsceneTeamSpokesmanID = 0;

static bool s_bFirstFrame = false;

static void contactCutSceneStartNewCutScene(SA_PARAM_NN_VALID CutsceneDef *pCutSceneDef)
{
	// Get the current player
	Entity *pEnt = entActivePlayerPtr();

	// Get the current dialog dialog
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	CutsceneDef *pNewCutScene;
	if (pCutSceneDef == NULL)
	{
		return;
	}

	// Clone the cutscene def
	pNewCutScene = StructClone(parse_CutsceneDef, pCutSceneDef);

	// Clean the dynamic data in the cutscene before we play another cutscene
	contactui_CleanUpCurrentCutscene(pNewCutScene);

	// Reset the elapsed time
	s_fCurrentCutSceneElapsedTime = 0.f;

	// Set the new cutscene
	s_pCurrentCutScene = pNewCutScene;

	s_bFirstFrame = true;

	// Store the team spokesman ID
	s_iCutsceneTeamSpokesmanID = pContactDialog ? pContactDialog->iTeamSpokesmanID : 0;

	gclCutsceneStartEx(gGCLState.pPrimaryDevice->contactcamera.camcenter, gGCLState.pPrimaryDevice->contactcamera.campyr, 0.f, NULL);

	// Load splines
	gclCutsceneLoadSplines(s_pCurrentCutScene->pPathList);
}

bool contactCutSceneCameraGetPosPyr(F32 timestep, Vec3 cameraPos /*out*/, Vec3 cameraPYR /*out*/, GfxSkyData *sky_data)
{
	// Get the current player
	Entity *pEntity = entActivePlayerPtr();

	// Get the dialog
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	// Get the cut-scene
	CutsceneDef *pCutSceneDef = pContactDialog ? GET_REF(pContactDialog->hCutSceneDef) : s_pCurrentCutScene;

	if (pCutSceneDef)
	{
		bool bReset = false;
		F32 fCutSceneLength;
		ContainerID iCurrentTeamSpokesmanID = 0;

		if (pContactDialog)
		{
			Entity * pHeadshotEnt = entFromEntityRefAnyPartition(pContactDialog->headshotEnt);
			Entity * pCamSrcEnt = entFromEntityRefAnyPartition(pContactDialog->cameraSourceEnt);

			// Get the current team spokesman
			iCurrentTeamSpokesmanID = pContactDialog->iTeamSpokesmanID;

			if (pHeadshotEnt)
			{
				DynDrawSkeleton *pSkeleton = dynDrawSkeletonFromGuid(pHeadshotEnt->dyn.guidDrawSkeleton);
				if(pSkeleton) {
					gfxEnsureAssetsLoadedForSkeleton(pSkeleton);
				}
			}

			if (pCamSrcEnt)
			{
				DynDrawSkeleton *pSkeleton = dynDrawSkeletonFromGuid(pCamSrcEnt->dyn.guidDrawSkeleton);
				if(pSkeleton) {
					gfxEnsureAssetsLoadedForSkeleton(pSkeleton);
				}
			}
		}

		if (s_pCurrentCutScene == NULL || pCutSceneDef->name != s_pCurrentCutScene->name || s_iCutsceneTeamSpokesmanID != iCurrentTeamSpokesmanID)
		{
			contactCutSceneStartNewCutScene(pCutSceneDef);
		}

		if (s_bFirstFrame)
		{
			bReset = true;
			s_bFirstFrame = false;
		}
		else
		{
			// Increment the time elapsed, but clamp at every Camera Path end point to ensure there is one tick on each Camera Path at low frame rates.
			s_fCurrentCutSceneElapsedTime = cutscene_IncrementElapsedTimeAndClamp(s_pCurrentCutScene, s_fCurrentCutSceneElapsedTime, timestep);
		}

		fCutSceneLength = cutscene_GetLength(s_pCurrentCutScene, true);
		s_fCurrentCutSceneElapsedTime = MIN(fCutSceneLength, s_fCurrentCutSceneElapsedTime);

		if(s_pCurrentCutScene->pPathList)
		{
			gclGetCutsceneCameraPathListPosPyr(s_pCurrentCutScene, s_fCurrentCutSceneElapsedTime, cameraPos, cameraPYR, sky_data, s_bFirstFrame, timestep);
			return true;
		}
		gclGetCutsceneCameraBasicPosPyr(s_pCurrentCutScene, cameraPos, cameraPYR);
		return true;
	}
	return false;
}

void contactui_CleanUpCurrentCutscene(CutsceneDef *pNextCutScene)
{
	if (s_pCurrentCutScene)
	{
		gclCleanDynamicDataEx(s_pCurrentCutScene, pNextCutScene);
	}
	StructDestroySafe(parse_CutsceneDef, &s_pCurrentCutScene);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ContactDialogCleanupCutscene");
void exprContactDialogCleanupCutscene(void)
{
	contactui_CleanUpCurrentCutscene(NULL);
	contactui_ResetToGameCamera();
}

void contactCutSceneCameraFunc(GfxCameraController *pCamera, GfxCameraView *pCameraView, F32 fElapsed, F32 fRealElapsed)
{
	// This will populate the pCamera center and pCamera PYR if there is an active cutscene
	if(contactCutSceneCameraGetPosPyr(fRealElapsed, pCamera->camcenter, pCamera->campyr, pCameraView->sky_data))
	{
		Mat4 xCameraMatrix;
		Vec3 postCamPYR;
		Vec3 postCamOffset;

		// Let the camera apply any effects to the camera
		gclCamera_GetShake(pCamera, postCamPYR, postCamOffset, fRealElapsed);

		// Create a camera matrix from camcenter and campyr
		createMat3YPR(xCameraMatrix, postCamPYR);
		{
			Vec3 vTemp;
			mulVecMat3(postCamOffset, xCameraMatrix, vTemp);
			addVec3(pCamera->camcenter, vTemp, xCameraMatrix[3]);
		}

		copyVec3(pCamera->camcenter, pCamera->camfocus);

		frustumSetCameraMatrix(&pCameraView->new_frustum, xCameraMatrix);

		gclCamera_DoEntityCollisionFade(NULL, xCameraMatrix[3]);

		// That's it, I've copied these lines to too many places. I'm going to have to do a proper refactor to make this just work.
		//  - ama
#if !PLATFORM_CONSOLE
		if (!gfxGetFullscreen())
		{
			ClipCursor(NULL);
		}
#endif
		mouseLock(0);
	}
	else
	{
		// Reset cut-scene variables
		s_fCurrentCutSceneElapsedTime = 0.f;
	}
}
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogReleaseCamera");
void exprContactDialogReleaseCamera(SA_PARAM_OP_VALID Entity *pEntity)
{
	contactui_ResetToGameCamera();
}

// Whenever the cut-scene system starts playing a cut-scene, it calls this function
void contactui_NotifyCutsceneStartOnClient(void)
{
	exprContactDialogCleanupCutscene();
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogSnapCameraToContactsHead");
void exprContactDialogSnapCameraToContactsHead(SA_PARAM_OP_VALID Entity *pEntity)
{
	InteractInfo* pInfo = SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo);
	Entity *pEntContact = NULL;
	GfxCameraController * pLastCamController = NULL;

	// This will clear up the control buffer which would prevent the player keep running.
	gclTurnOffAllControlBits();

	s_bIsContactHeadCameraInUse = false;

	if (pInfo == NULL ||
		pInfo->pContactDialog == NULL)
	{
		return;
	}

	// We need either a headshot entity or a camera source
	if ((pEntContact = entFromEntityRefAnyPartition(pInfo->pContactDialog->headshotEnt)) == NULL && vec3IsZero(pInfo->pContactDialog->vecCameraSourcePos))
	{
		return;
	}

	// Reset the cutscene timer
	s_fCurrentCutSceneElapsedTime = 0.f;

	s_bIsContactHeadCameraInUse = true;

	if (gGCLState.pPrimaryDevice->activecamera == &gGCLState.pPrimaryDevice->contactcamera)
		return;

	// Set the active camera
	gGCLState.pPrimaryDevice->activecamera = &gGCLState.pPrimaryDevice->contactcamera;

	// Lock the camera and player
	gGCLState.bLockPlayerAndCamera = true;

	// Copy position and rotation from game camera
	gfxCameraControllerCopyPosPyr(&gGCLState.pPrimaryDevice->gamecamera, &gGCLState.pPrimaryDevice->contactcamera);
	gclCutsceneStartEx(gGCLState.pPrimaryDevice->contactcamera.camcenter, gGCLState.pPrimaryDevice->contactcamera.campyr, 0.f, NULL);
}

AUTO_STRUCT;
typedef struct CutsceneRef
{
	REF_TO(CutsceneDef) hCutsceneDef;
} CutsceneRef;

//This command can be used to play a the contact cutscene outside of an actual contact dialog.Back
//Calling it will put you in the contact cutscene, and calling it again will take you out.
//The first time it is used you made need to call it twice because the cutscene needs to load.
AUTO_COMMAND ACMD_ACCESSLEVEL(9) ACMD_NAME(RunContactStyleCutscene);
void runContactStyleCutscene(const char *pchCutsceneName)
{
	static bool bPlaying = false;
	static CutsceneRef cutscene;

	SET_HANDLE_FROM_STRING(g_hCutsceneDict, pchCutsceneName, cutscene.hCutsceneDef);

	if (!bPlaying)
	{
		CutsceneDef *pDef = GET_REF(cutscene.hCutsceneDef);
		if(pDef)
		{
			contactCutSceneStartNewCutScene(pDef);
			gGCLState.pPrimaryDevice->activecamera = &gGCLState.pPrimaryDevice->contactcamera;
			bPlaying = true;
		}
	} 
	else
	{
		contactui_CleanUpCurrentCutscene(NULL);
		gGCLState.pPrimaryDevice->activecamera = &gGCLState.pPrimaryDevice->gamecamera;
		contactui_ResetToGameCamera();
		bPlaying = false;
	}
}

// ----------------------------------------------------------------------------
//  ContactUI functions
//
//  TODO: Anything above this should probably be removed or moved to a
//  different file
// ----------------------------------------------------------------------------

typedef struct ContactDialogSoundData
{
	const char *pchPreviousSound;
	const char **ppchPreviousSounds;
	SoundSource *pPreviousSource;
	const char *pchPreviousPhraseSound;

	char pchPreviousVoicePath[128];
	char pchPreviousPhrasePath[128];

	EntityRef speakingEntity;
} ContactDialogSoundData;

static ContactDialogSoundData s_ContactSoundData = {0};

// Play a sound for the client without repeating THE MOST RECENT sound that Contact until the player closes and reenters the Contact's Dialog.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayContactDialogSound");
void exprClientPlayContactDialogSound(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	// voice/phrase to play?
	if(pContactDialog)
	{
		// since this is called once per frame
		// see if things have changed
		if(	pContactDialog->pchVoicePath && pContactDialog->pchVoicePath[0] &&
			pContactDialog->pchPhrasePath && pContactDialog->pchPhrasePath[0] &&
			((strcmp(pContactDialog->pchVoicePath, s_ContactSoundData.pchPreviousVoicePath) != 0) || strcmp(pContactDialog->pchPhrasePath, s_ContactSoundData.pchPreviousPhrasePath) != 0) )
		{
			EntityRef entRef = pContactDialog->cameraSourceEnt == 0 ? pContactDialog->headshotEnt : pContactDialog->cameraSourceEnt;

			sndPlayRandomPhraseWithVoice(pContactDialog->pchPhrasePath, pContactDialog->pchVoicePath, entRef);

			strcpy(s_ContactSoundData.pchPreviousVoicePath, pContactDialog->pchVoicePath);
			strcpy(s_ContactSoundData.pchPreviousPhrasePath, pContactDialog->pchPhrasePath);
		}
	}

	// Stop previous sound (if any)
	if (s_ContactSoundData.pchPreviousSound &&
		((!pContactDialog && g_audio_state.bMuteVOonContactEnd) || (pContactDialog && ( !pContactDialog->pchSoundToPlay || (stricmp(s_ContactSoundData.pchPreviousSound, pContactDialog->pchSoundToPlay) != 0))))) {
		sndKillSourceIfPlaying(s_ContactSoundData.pPreviousSource, true);
		s_ContactSoundData.pchPreviousSound = NULL;
	}

	// if contact disappears, clear the strings
	if(!pContactDialog && g_audio_state.bMuteVOonContactEnd)
	{
		s_ContactSoundData.pchPreviousVoicePath[0] = '\0';
		s_ContactSoundData.pchPreviousPhrasePath[0] = '\0';
		s_ContactSoundData.pchPreviousSound = NULL;
	}

	// Start new sound (if any)
	if (!s_ContactSoundData.pchPreviousSound && pContactDialog && pContactDialog->pchSoundToPlay) {
		EntityRef entRef = pContactDialog->cameraSourceEnt == 0 ? pContactDialog->headshotEnt : pContactDialog->cameraSourceEnt;

		s_ContactSoundData.pPreviousSource = sndPlayFromEntity(pContactDialog->pchSoundToPlay, entRef, exprContextGetBlameFile(pContext), false);
		s_ContactSoundData.speakingEntity = entRef;

		s_ContactSoundData.pchPreviousSound = pContactDialog->pchSoundToPlay; // this is a pooled string
	}
}

// Play a sound for the client without repeating ANY sounds for that Contact until the player closes and reenters the Contact's Dialog.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("PlayContactDialogSoundNoRepeats");
void PlayContactDialogSoundNoRepeats(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	// voice/phrase to play?
	if(pContactDialog)
	{
		// since this is called once per frame
		// see if things have changed
		if(	pContactDialog->pchVoicePath && pContactDialog->pchVoicePath[0] &&
			pContactDialog->pchPhrasePath && pContactDialog->pchPhrasePath[0] &&
			((strcmp(pContactDialog->pchVoicePath, s_ContactSoundData.pchPreviousVoicePath) != 0) || strcmp(pContactDialog->pchPhrasePath, s_ContactSoundData.pchPreviousPhrasePath) != 0) )
		{
			EntityRef entRef = pContactDialog->cameraSourceEnt == 0 ? pContactDialog->headshotEnt : pContactDialog->cameraSourceEnt;

			sndPlayRandomPhraseWithVoice(pContactDialog->pchPhrasePath, pContactDialog->pchVoicePath, entRef);

			strcpy(s_ContactSoundData.pchPreviousVoicePath, pContactDialog->pchVoicePath);
			strcpy(s_ContactSoundData.pchPreviousPhrasePath, pContactDialog->pchPhrasePath);
		}
	}

	// Stop previous sound (if any)
	if (s_ContactSoundData.pchPreviousSound &&
		((!pContactDialog && g_audio_state.bMuteVOonContactEnd) || (pContactDialog && ( !pContactDialog->pchSoundToPlay || (stricmp(s_ContactSoundData.pchPreviousSound, pContactDialog->pchSoundToPlay) != 0))))) {
			sndKillSourceIfPlaying(s_ContactSoundData.pPreviousSource, true);
			s_ContactSoundData.pchPreviousSound = NULL;
	}

	// if contact disappears, clear the strings
	if(!pContactDialog && g_audio_state.bMuteVOonContactEnd)
	{
		s_ContactSoundData.pchPreviousVoicePath[0] = '\0';
		s_ContactSoundData.pchPreviousPhrasePath[0] = '\0';
		s_ContactSoundData.pchPreviousSound = NULL;
		eaClearFast(&s_ContactSoundData.ppchPreviousSounds);
	}

	// Start new sound (if any)
	if (pContactDialog && pContactDialog->pchSoundToPlay && (!s_ContactSoundData.ppchPreviousSounds || 0 > eaFindString(&s_ContactSoundData.ppchPreviousSounds, pContactDialog->pchSoundToPlay))) {
		EntityRef entRef = pContactDialog->cameraSourceEnt == 0 ? pContactDialog->headshotEnt : pContactDialog->cameraSourceEnt;

		s_ContactSoundData.pPreviousSource = sndPlayFromEntity(pContactDialog->pchSoundToPlay, entRef, exprContextGetBlameFile(pContext), false);
		s_ContactSoundData.speakingEntity = entRef;

		s_ContactSoundData.pchPreviousSound = pContactDialog->pchSoundToPlay; // this is a pooled string
		eaPush(&s_ContactSoundData.ppchPreviousSounds, pContactDialog->pchSoundToPlay); // this is a pooled string
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("KillContactDialogSound");
void exprKillContactDialogSound(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	if (s_ContactSoundData.pchPreviousSound &&
		(!pContactDialog || !pContactDialog->pchSoundToPlay || (stricmp(s_ContactSoundData.pchPreviousSound, pContactDialog->pchSoundToPlay) != 0))) {
			sndKillSourceIfPlaying(s_ContactSoundData.pPreviousSource, true);
			s_ContactSoundData.pchPreviousSound = NULL;
	}

	// if contact disappears, clear the strings
	if(!pContactDialog)
	{
		s_ContactSoundData.pchPreviousVoicePath[0] = '\0';
		s_ContactSoundData.pchPreviousPhrasePath[0] = '\0';
		s_ContactSoundData.pchPreviousSound = NULL;
		eaClearFast(&s_ContactSoundData.ppchPreviousSounds);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("IsContactDialogSoundPlaying");
bool exprIsContactDialogSoundPlaying()
{
	return sndEventIsPlaying(s_ContactSoundData.pchPreviousSound);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ContactDialogGetSpeakingEntity");
SA_RET_OP_VALID Entity *exprContactDialogGetSpeakingEntity(ExprContext *pContext)
{
	if (pContext)
	{
		return entFromEntityRef(exprContextGetPartition(pContext), s_ContactSoundData.speakingEntity);
	}

	return NULL;
}

// Returns the state of the Contact interaction, defined in contact_common.h
// Returns "None" if there is no current contact interaction.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetContactDialogScreen");
const char *exprGetContactDialogScreen(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_ENUM_STR_GETTER(pEntity, ContactScreenType, screenType, "None")
}

// Returns true if the current cutscene for the contact dialog is loaded. Function still returns true, if there is no cutscene.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogCutsceneLoaded");
bool exprContactDialogCutsceneLoaded(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	if (pDialog)
	{
		CutsceneDef *pCutsceneDef = GET_REF(pDialog->hCutSceneDef);

		if (pCutsceneDef)
		{
			if (s_pCurrentCutScene == NULL)
			{
				contactCutSceneStartNewCutScene(pCutsceneDef);
			}
			return gclCutscenePreLoad(s_pCurrentCutScene);
		}
		else if(REF_IS_SET_BUT_ABSENT(pDialog->hCutSceneDef))
		{
			return false;
		}
		else
		{
			return true;
		}
	}

	return false;
}

// Returns true if the graphics system thinks things are done loading
// for the new camera position.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogScreenReadyToFadeIn");
bool exprContactDialogScreenReadyToFadeIn(SA_PARAM_OP_VALID Entity *pEntity, F32 fMinTimeSinceLoad) {
	return !gfxIsLoadingForContact(fMinTimeSinceLoad);
}

// Returns the state of the Contact interaction, defined in contact_common.h
// Returns ContactScreenTypeNone if there is no interaction.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetContactDialogScreenType");
S32 exprGetContactDialogScreenType(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, screenType, ContactScreenType_None);
}

// Returns the state of the Contact interaction, defined in contact_common.h
// Returns ContactDialogState_None if there is no interaction.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetContactDialogState");
S32 exprGetContactDialogState(SA_PARAM_OP_VALID Entity *pEntity)
{
	InteractInfo* pInfo = SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo);

	if(pInfo && pInfo->pContactDialog)
	{
		int i;
		for (i = eaSize(&pInfo->pContactDialog->eaOptions)-1; i >= 0; --i)
		{
			if (pInfo->pContactDialog->eaOptions[i]->bShowRewardChooser) return ContactDialogState_ViewCompleteMission;
		}
	}
	return ContactDialogState_None;
}

// Returns the mission credit type of the current contact interaction
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetContactMissionCreditType");
const char* exprGetContactMissionCreditType(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_ENUM_STR_GETTER(pEntity, MissionCreditType, eMissionCreditType, "Primary");
}

// Returns the mission credit type of the current contact interaction
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("IsContactOfferingSharedMission");
bool exprIsContactOfferingSharedMission(SA_PARAM_OP_VALID Entity *pEntity)
{
	InteractInfo* pInfo = SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo);

	if(pInfo && pInfo->pContactDialog)
		return (pInfo->pSharedMission != NULL);
	else
		return false;
}

// DEPRECATED; Use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("HeadshotFromContactDialog");
SA_RET_OP_VALID BasicTexture *gclExpr_HeadshotFromContactDialog(SA_PARAM_OP_VALID Entity *pPlayer,
															 SA_PARAM_OP_VALID BasicTexture *pTexture,
															 F32 fWidth, F32 fHeight)
{
	return NULL;
}

// DEPRECATED; Use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("AnimatedHeadshotFromContactDialog");
SA_RET_OP_VALID BasicTexture *gclExpr_AnimatedHeadshotFromContactDialog(SA_PARAM_OP_VALID Entity *pPlayer,
																	 SA_PARAM_OP_VALID BasicTexture *pTexture,
																	 F32 fWidth, F32 fHeight, F32 fDeltaTime)
{
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetVoteCountByDialogKey");
U32 exprContactDialogGetVoteCountByDialogKey(const char *pchDialogKey)
{
	Entity* pEnt = entActivePlayerPtr();
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 iCount = 0;

	if (pContactDialog)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pContactDialog->eaTeamDialogVotes, TeamDialogVote, pVote)
		{
			if (pVote && stricmp(pVote->pchDialogKey, pchDialogKey) == 0)
			{
				++iCount;
			}
		}
		FOR_EACH_END
	}
	return iCount;
}

static void ContactIndicatorStringListToEnumArray(ExprContext *pContext, const char* pchList, S32 **peaiOutList)
{
	if (peaiOutList)
	{
		int iContactEnumMin;
		int iContactEnumMax;
		DefineGetMinAndMaxInt(ContactIndicatorEnum, &iContactEnumMin, &iContactEnumMax);
		eaiClearFast(peaiOutList);
		eaiSetSize(peaiOutList, iContactEnumMax+1);
		if (pchList)
		{
			char *pchTypesCopy;
			char *pchContext;
			char *pchStart;
			strdup_alloca(pchTypesCopy, pchList);
			pchStart = strtok_r(pchTypesCopy, " ,\t\r\n", &pchContext);
			do
			{
				S32 iEnumVal = StaticDefineIntGetInt(ContactIndicatorEnum, pchStart);
				if (iEnumVal == -1)
				{
					ErrorFilenamef(exprContextGetBlameFile(pContext), "ContactIndicator %s not recognized", pchStart);
				}
				else
				{
					(*peaiOutList)[iEnumVal] = 1;
				}
			} while (pchStart = strtok_r(NULL, " ,\t\r\n", &pchContext));
		}
	}
}
static void ContactImageMenuDialogOptionAddUIDetails(ContactDialogOption* dialogOption)
{
	Entity* pEnt = entActivePlayerPtr();
	MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
	int i;
	MinimapWaypoint* goldenPathWaypoint = goldenPath_GetStatus()->pTargetWaypoint;
	char* estrBuffer = NULL;
	MissionDef** eaCheckedMissions = NULL;
	estrCreate(&estrBuffer);
	eaCreate(&eaCheckedMissions);

	//check if it's my map
	if(zmapInfoGetCurrentName(NULL) == dialogOption->pchMapName){
		dialogOption->bYouAreHere = true;
	}
	if(dialogOption->pchConfirmText)
	{
		estrClear(&dialogOption->pchConfirmText);
	}

	//check if any of our waypoints are on that map
	for(i=0; i<eaSize(&pMissionInfo->waypointList); i++)
	{
		if(pMissionInfo->waypointList[i]->pchMissionRefString && pMissionInfo->waypointList[i]->pchDestinationMap
			&& pMissionInfo->waypointList[i]->pchDestinationMap == dialogOption->pchMapName)
		{
			//a mission is here.

			//	a mission may have many waypoints.  Only deal with each root mission once.
			MissionDef *pMissionDef = missiondef_DefFromRefString(pMissionInfo->waypointList[i]->pchMissionRefString);
			MissionDef *pMissionRootDef = missiondef_GetRootDef(pMissionDef);
			if( eaFind(&eaCheckedMissions, pMissionRootDef) == -1)
			{
				eaPush(&eaCheckedMissions, pMissionRootDef);
			}
			else
			{
				continue;
			}

			//	The text assembly is kinda hacky here, but there's just one text field easily available to stick it into.
			//	So give the UI just a list of quests with <br> tags for each dialog option.  UI can add tags before or after the list for text formatting.
			//	They might also want to format the Golden Path quest, so we get a message before and after that just so its formatting can be set up in a data message file.
			dialogOption->bWaypointHere = true;

			if (goldenPathWaypoint && !stricmp(goldenPathWaypoint->pchMissionRefString, pMissionInfo->waypointList[i]->pchMissionRefString))
			{
				//your golden path quest is here.
				dialogOption->bGoldenPathWaypointHere = true;

				estrAppend2(&dialogOption->pchConfirmText, "<br>");
				estrAppend2(&dialogOption->pchConfirmText, entTranslateMessageKey(pEnt,"ContactUI.PreGoldenPathQuest.TooltipMarkup"));
				GenMapIconGetMissionLabel(pMissionInfo->waypointList[i], &estrBuffer);
				estrAppend(&dialogOption->pchConfirmText, &estrBuffer);	//GenMapIconGetMissionLabel clears the estr.
				estrAppend2(&dialogOption->pchConfirmText, entTranslateMessageKey(pEnt,"ContactUI.PostGoldenPathQuest.TooltipMarkup"));
			}
			else
			{
				estrAppend2(&dialogOption->pchConfirmText, "<br>");
				GenMapIconGetMissionLabel(pMissionInfo->waypointList[i], &estrBuffer);
				estrAppend(&dialogOption->pchConfirmText, &estrBuffer);	//GenMapIconGetMissionLabel clears the estr.
			}

		}
	}
	eaDestroy(&eaCheckedMissions);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillContactDialogOptionListWithExclusions");
void exprFillContactDialogOptionListWithExclusions(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, const char* pchExclude, bool bFilterBackOption)
{
	static ContactDialogOption **eaOptions = NULL;
	S32 i;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	static S32 *eaiTypes = NULL;

	eaClear(&eaOptions);

	if (pContactDialog)
	{
		ContactIndicatorStringListToEnumArray(pContext, pchExclude, &eaiTypes);

		for (i = 0; i < eaSize(&pContactDialog->eaOptions); i++)
		{
			ContactDialogOption *pOption = pContactDialog->eaOptions[i];
			S32 iTeamMembersEligibleToSee = ea32Size(&pOption->piTeamMembersEligibleToSee);

			if ((bFilterBackOption && pContactDialog->eaOptions[i]->bIsDefaultBackOption)
				|| eaiTypes[pOption->eType])
			{
				continue;
			}

			if (iTeamMembersEligibleToSee > 0)
			{
				// We're in a critical team dialog. Make sure this player is eligible to see this dialog option
				S32 j;
				for (j = 0; j < iTeamMembersEligibleToSee; j++)
				{
					if (pContactDialog->eaOptions[i]->piTeamMembersEligibleToSee[j] == pEntity->myContainerID ||
						exprContactDialogGetVoteCountByDialogKey(pContactDialog->eaOptions[i]->pchKey) > 0)
					{
						eaPush(&eaOptions, pContactDialog->eaOptions[i]);
						
						if(pContactDialog->screenType == ContactScreenType_ImageMenu)
						{
							ContactImageMenuDialogOptionAddUIDetails(pContactDialog->eaOptions[i]);
						}
						break;
					}
				}
			}
			else
			{
				eaPush(&eaOptions, pContactDialog->eaOptions[i]);

				if(pContactDialog->screenType == ContactScreenType_ImageMenu)
					ContactImageMenuDialogOptionAddUIDetails(pContactDialog->eaOptions[i]);
			}
		}
	}
	ui_GenSetList(pGen, (void ***)&eaOptions, parse_ContactDialogOption);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillContactDialogOptionList");
void exprFillContactDialogOptionList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bFilterBackOption)
{
	exprFillContactDialogOptionListWithExclusions(pContext, pEntity, NULL, bFilterBackOption);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillContactDialogOptionListFiltered");
void exprFillContactDialogOptionListFiltered(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bFilterBackOption)
{
	if (exprGenContactDialogMustShowSingleContinueOption(pEntity))
	{
		static ContactDialogOption **eaOptions = NULL;
		UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
		ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

		if (pContactDialog && eaSize(&pContactDialog->eaOptions) > 0)
		{
			eaClear(&eaOptions);
			eaPush(&eaOptions, pContactDialog->eaOptions[0]);

			ui_GenSetList(pGen, (void ***)&eaOptions, parse_ContactDialogOption);
		}
	}
	else
	{
		exprFillContactDialogOptionList(pContext, pEntity, bFilterBackOption);
	}
}

static S32 FillContactDialogMissionListInternal(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bFilterBackOption, const char* pchContactIndicatorTypes, bool bFillList)
{
	static ContactDialogOption **s_eaOptions = NULL;
	S32 i, iCount = 0;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	static S32 *eaiTypes = NULL;

	eaClearFast(&s_eaOptions);

	if (pContactDialog)
	{
		ContactIndicatorStringListToEnumArray(pContext, pchContactIndicatorTypes, &eaiTypes);
		for (i = 0; i < eaSize(&pContactDialog->eaOptions); i++)
		{
			if ((!bFilterBackOption || !pContactDialog->eaOptions[i]->bIsDefaultBackOption)
				&& eaiTypes[pContactDialog->eaOptions[i]->eType]!=0)
			{
				if (bFillList)
				{
					eaPush(&s_eaOptions, pContactDialog->eaOptions[i]);
				}

				iCount++;
			}
		}
	}

	if (bFillList)
		ui_GenSetListSafe(pGen, &s_eaOptions, ContactDialogOption);
	
	return iCount;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("FillContactDialogMissionList");
S32 exprFillContactDialogMissionList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bFilterBackOption, const char* pchContactIndicatorTypes)
{
	return FillContactDialogMissionListInternal(pContext, pEntity, bFilterBackOption, pchContactIndicatorTypes, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GetContactDialogMissionListSize");
S32 exprGetContactDialogMissionListSize(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pEntity, bool bFilterBackOption, const char* pchContactIndicatorTypes)
{
	return FillContactDialogMissionListInternal(pContext, pEntity, bFilterBackOption, pchContactIndicatorTypes, false);
}

static ContactDialogOption * contactUI_GetDialogOptionByKey(SA_PARAM_NN_VALID Entity *pEnt, SA_PARAM_NN_STR const char *pchDialogKey)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	if (pDialog)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pDialog->eaOptions, ContactDialogOption, pOption)
		{
			if (stricmp(pOption->pchKey, pchDialogKey) == 0)
			{
				return pOption;
			}
		}
		FOR_EACH_END
	}

	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetDialogTextByKey");
const char * exprContactDialogGetDialogTextByKey(SA_PARAM_OP_VALID Entity *pEntity, const char *pchDialogKey)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog)
	{
		ContactDialogOption *pOption = contactUI_GetDialogOptionByKey(pEntity, pchDialogKey);

		if (pOption)
		{
			if ((pOption->pchDisplayString == NULL || pchDialogKey[0] == '\0') && pOption == pDialog->eaOptions[0])
			{
				return TranslateMessageKey("ContactUI.Continue");
			}
			else
			{
				return NULL_TO_EMPTY(pOption->pchDisplayString);
			}
		}
	}
	return "";
}

static bool contactUI_CanChooseDialogOption(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID ContactDialogOption *pDialogOption, bool bDoSpecialSpokesmanCheck)
{
	if (pEntity && pDialogOption)
	{
		S32 iTeamMembersEligibleToSee = ea32Size(&pDialogOption->piTeamMembersEligibleToSee);
		S32 iTeamMembersEligibleToInteract = ea32Size(&pDialogOption->piTeamMembersEligibleToInteract);

		if (iTeamMembersEligibleToInteract > 0 || iTeamMembersEligibleToSee > 0)
		{
			ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

			// We're in a critical team dialog. Check if this player is allowed to see and choose this option
			S32 i;

			bool bCanSee = false;

			if (bDoSpecialSpokesmanCheck &&
				pContactDialog->iTeamSpokesmanID == pEntity->myContainerID &&
				exprContactDialogGetVoteCountByDialogKey(pDialogOption->pchKey) > 0)
			{
				// Dialog options become selectable for the spokesman if someone else voted for that option
				return true;
			}

			for (i = 0; i < iTeamMembersEligibleToSee; i++)
			{
				if (pDialogOption->piTeamMembersEligibleToSee[i] == pEntity->myContainerID)
				{
					bCanSee = true;
					break;
				}
			}

			if (!bCanSee)
			{
				return false;
			}

			for (i = 0; i < iTeamMembersEligibleToInteract; i++)
			{
				if (pDialogOption->piTeamMembersEligibleToInteract[i] == pEntity->myContainerID)
				{
					return true;
				}
			}
			return false;
		}
		else
		{
			return !pDialogOption->bCannotChoose;
		}
	}
	return false;
}

// Send a response back to the server.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogRespond");
void exprContactDialogRespond(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID const char *pchOptionKey, const char* pchRewardChoices)
{
	static ContactRewardChoices s_eaRewardChoices = {0};
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	U32 iOptionAssistEntID = 0;

	if (pDialog && pDialog->iParticipatingTeamMemberCount && g_ContactConfig.bServerDecidesChoiceForTeamDialogs)
	{
		return;
	}
	else if (pDialog)
	{
		// Get the option player responded
		ContactDialogOption *pOption = contactUI_GetDialogOptionByKey(pEntity, pchOptionKey);

		if (pOption && !contactUI_CanChooseDialogOption(pEntity, pOption, false))
		{
			// We're in a critical team dialog. If another team member enabled this option for the team spokesman we have to find out who.
			// If there is more than one team member who voted for this option we choose the first one.
			FOR_EACH_IN_EARRAY_FORWARDS(pDialog->eaTeamDialogVotes, TeamDialogVote, pVote)
			{
				if (pVote->iEntID != pEntity->myContainerID && stricmp_safe(pVote->pchDialogKey, pchOptionKey) == 0)
				{
					iOptionAssistEntID = pVote->iEntID;
					break;
				}
			}
			FOR_EACH_END

			if (iOptionAssistEntID == 0)
			{
				// No entity voted for this option
				return;
			}
		}
	}

	if(pchRewardChoices && pchRewardChoices[0])
	{
		char *pchContext;
		char *pchChoices = NULL;
		char *pchStart;
		strdup_alloca(pchChoices, pchRewardChoices);
		pchStart = strtok_r(pchChoices, " \t\n\r", &pchContext);

		do
		{
			eaPush(&s_eaRewardChoices.ppItemNames, StructAllocString(pchStart));
		} while (pchStart = strtok_r(NULL, " \t\n\r", &pchContext));
	}

	if (iOptionAssistEntID > 0)
	{
		s_bSpokesmanWillChange = true;
	}

	ServerCmd_ContactResponse(pchOptionKey, &s_eaRewardChoices, iOptionAssistEntID);

	eaClearEx(&s_eaRewardChoices.ppItemNames, StructFreeString);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetDialogIndexByKey");
U32 exprContactDialogGetDialogIndexByKey(SA_PARAM_OP_VALID Entity *pEntity, const char *pchDialogKey)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pDialog->eaOptions, ContactDialogOption, pOption)
		{
			if (stricmp(pOption->pchKey, pchDialogKey) == 0)
				return FOR_EACH_IDX(pDialog->eaOptions, pOption);
		}
		FOR_EACH_END
		return -1;
	}
	return -1;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetDialogKeyByIndex");
const char * exprContactDialogGetDialogKeyByIndex(SA_PARAM_OP_VALID Entity *pEntity, U32 iDialogOptionIndex)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog && iDialogOptionIndex >= 0)
	{
		U32 iOptionCount = eaSize(&pDialog->eaOptions);
		if (iDialogOptionIndex < iOptionCount)
		{
			return pDialog->eaOptions[iDialogOptionIndex]->pchKey;
		}
		return NULL;
	}
	return NULL;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogRespondByIndex");
void exprContactDialogRespondByIndex(SA_PARAM_OP_VALID Entity *pEntity, U32 iDialogOptionIndex)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog && iDialogOptionIndex >= 0)
	{
		U32 iOptionCount = eaSize(&pDialog->eaOptions);
		if (iDialogOptionIndex < iOptionCount)
		{
			exprContactDialogRespond(pEntity, pDialog->eaOptions[iDialogOptionIndex]->pchKey, "");
		}
	}
}

// Send a response back to the server.
// This uses the default "Back" option for whatever state the dialog is in.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogBack");
bool exprContactDialogBack(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog){
		int i;
		for (i = eaSize(&pDialog->eaOptions)-1; i >= 0; --i){
			if (pDialog->eaOptions[i]->bIsDefaultBackOption){
				ServerCmd_ContactResponse(pDialog->eaOptions[i]->pchKey, NULL, 0);
				return true;
			}
		}
	}
	return false;
}

// Send a response back to the server.
// Close the current contact dialog
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogEnd");
void exprContactDialogEnd(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog){
		ServerCmd_ContactDialogEndServer();
	}
}

// This starts interaction with the last recently completed dialog.
// If it's not possible to do so, it will return false.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactInteractWithLastRecentlyCompletedDialog");
bool exprContactInteractWithLastRecentlyCompletedDialog(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog && pDialog->bLastCompletedDialogIsInteractable) {
		ServerCmd_ContactInteractWithLastRecentlyCompletedDialog();
		return true;
	}
	return false;
}

// Stop talking to the current contact.  Safe to use if there is no current contact.
// This should be called instead of "ContactDialogEndServer" so that the client can
// validate that the player is actually in a contact dialog.  Otherwise, there are
// some weird edge cases when multiple Contact Dialogs happen close together.
AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME("ContactDialogEnd");
void ContactDialogEndClient(Entity* pEntity)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog){
		ServerCmd_ContactDialogEndServer();
	}

	s_bSpokesmanWillChange = false;
}



// DEPRECATED; Use UIGenPaperdoll
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactHeadshotStyleDisplayBackgroundOnly");
bool exprContactHeadshotStyleDisplayBackgroundOnly(SA_PARAM_OP_VALID Entity *pEntity)
{
	return false;
}

// DEPRECATED; Use UIGenPaperdoll
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactHeadshotStyleGetBackgroundImage");
const char* exprContactHeadshotStyleGetBackgroundImage(SA_PARAM_OP_VALID Entity *pEntity)
{
	return "";
}

// Returns the number of options
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactGetNumOptions");
int exprContactGetNumOptions(SA_PARAM_OP_VALID Entity *pEntity)
{
	InteractInfo* info = SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo);

	if(info && info->pContactDialog)
	{
		return eaSize(&info->pContactDialog->eaOptions);
	}

	return 0;
}

// Indicates if the dialog is view only
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactIsViewOnly");
bool exprContactIsViewOnly(SA_PARAM_OP_VALID Entity *pEntity)
{
	InteractInfo* info = SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo);

	if (!team_IsWithTeam(pEntity))
	{
		return false;
	}

	if (info && info->pContactDialog && info->pContactDialog->bViewOnlyDialog)
	{
		return true;
	}
	else if (info && info->pContactDialog && info->pContactDialog->bIsTeamSpokesman)
	{
		// The dialog options are disabled for the team spokesman after he makes a selection
		return s_pchLastDialogChoiceKey != NULL && info->pContactDialog->iParticipatingTeamMemberCount > 0;
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactGetNumberOfParticipants");
bool exprContactGetNumberOfParticipants(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog* pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	return pDialog ? pDialog->iParticipatingTeamMemberCount : 0;
}

// Indicates if the given dialog option is selected by the team spokesman
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactIsDialogSelectedByTeamSpokesman");
bool exprContactIsDialogSelectedByTeamSpokesman(const char *pchDialogKey)
{
	if (pchDialogKey == NULL)
		return false;

	return s_pchLastDialogChoiceKey ? stricmp(s_pchLastDialogChoiceKey, pchDialogKey) == 0 : false;
}

// Indicates if the given dialog option is selected by the team spokesman
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactIsPlayerTeamSpokesman");
bool exprContactIsPlayerTeamSpokesman(Entity *pEnt)
{
	return pEnt && pEnt->pTeam && pEnt->pTeam->bIsTeamSpokesman;
}

// Indicates if the player is in a team dialog
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactIsInTeamDialogMode");
bool exprContactIsInTeamDialogMode(SA_PARAM_OP_VALID Entity *pEnt)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	return pDialog && (pDialog->bIsTeamSpokesman || pDialog->bViewOnlyDialog);
}

// Returns the icon name for a given option
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactGetOptionIconName");
const char *exprContactGetOptionIconName(ExprContext* pContext, SA_PARAM_OP_VALID ContactDialogOption *pOption)
{
	static char pStr[256];

	if (pOption) {
		sprintf(pStr, "ContactIcon_%s", StaticDefineIntRevLookup(ContactIndicatorEnum, pOption->eType));
		return exprContextAllocString(pContext, pStr);
	}

	return "";
}

// Returns the display name of the current contact
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactGetName");
const char *exprContactGetName(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, pchContactDispName, NULL);
}

// Sets the GenData for a UIGen from a ContactDialogOption Index
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenContactDialogMustShowSingleContinueOption");
bool exprGenContactDialogMustShowSingleContinueOption(SA_PARAM_OP_VALID Entity *pEntity)
{
	S32 iOptionCount;
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	if (pDialog == NULL)
		return false;

	iOptionCount = eaSize(&pDialog->eaOptions);

	if (iOptionCount == 1)
	{
		// Return true if there is a single continue option or there is a special dialog with single option
		return strEndsWith(pDialog->eaOptions[0]->pchKey, ".Continue") || strStartsWith(pDialog->eaOptions[0]->pchKey, "SpecialDialog.");
	}
	else if (iOptionCount == 2)
	{
		// Return true if there is a special dialog with back and continue buttons or mission turn in dialog with back and continue buttons
		return (stricmp(pDialog->eaOptions[0]->pchKey, "SpecialDialog.Continue") == 0 && stricmp(pDialog->eaOptions[1]->pchKey, "SpecialDialog.Back") == 0) ||
			(stricmp(pDialog->eaOptions[0]->pchKey, "ViewCompleteMission.Continue") == 0 && stricmp(pDialog->eaOptions[1]->pchKey, "ViewCompleteMission.Back") == 0);
	}

	return false;
}

// Sets the GenData for a UIGen from a ContactDialogOption Index
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("GenSetDataContactDialogOption");
void exprGenSetDataContactDialogOption(SA_PARAM_OP_VALID UIGen *pGen, SA_PARAM_OP_VALID Entity *pEntity, int iOptionIndex)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pGen)
		ui_GenSetPointer(pGen, NULL, parse_ContactDialogOption);
	if (pDialog){
		ContactDialogOption *pOption = eaGet(&pDialog->eaOptions, iOptionIndex);
		if (pOption && pGen){
			ui_GenSetPointer(pGen, pOption, parse_ContactDialogOption);
		}
	}
}

// Returns true if this mission rewards any items/xp/etc., not counting choosable rewards
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogHasNonChoosableReward");
bool exprContactContactDialogHasNonChoosableReward(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 i;
	if(pContactDialog)
	{
		for(i=0; i<eaSize(&pContactDialog->eaRewardBags); i++)
		{
			InventoryBag* pRewardBag = pContactDialog->eaRewardBags[i];
			if(inv_bag_CountItems(pRewardBag, NULL) && pRewardBag->pRewardBagInfo->PickupType != kRewardPickupType_Choose)
			{
				int j;
				Item **eaRewardItemList = NULL;
				// We need to make sure not all the items are silent
				inv_bag_GetSimpleItemList(pRewardBag, &eaRewardItemList, false);
				for (j=0; j<eaSize(&eaRewardItemList); j++)
				{
					ItemDef *pItemDef = GET_REF(eaRewardItemList[j]->hItem);
					if (pItemDef && !(pItemDef->flags & kItemDefFlag_Silent))
					{
						eaDestroy(&eaRewardItemList);
						return(true);
					}
				}
				eaDestroy(&eaRewardItemList);
			}
		}
	}
	return false;
}

// Returns true if this mission has any choosable rewards
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogHasChoosableReward");
bool exprContactContactDialogHasChoosableReward(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, pContactDialog);
	S32 i;
	if(pContactDialog)
	{
		for(i=0; i<eaSize(&pContactDialog->eaRewardBags); i++)
		{
			InventoryBag* pRewardBag = pContactDialog->eaRewardBags[i];
			if(pRewardBag->pRewardBagInfo && inv_bag_CountItems(pRewardBag, NULL) && pRewardBag->pRewardBagInfo->PickupType == kRewardPickupType_Choose && pRewardBag->pRewardBagInfo->NumPicks > 0)
				return true;
		}
	}
	return false;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogFillRewardList");
void exprContactDialogFillRewardList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	static Item **s_eaRewardItemList = NULL;
	InteractInfo *info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	S32 i;

	eaClear(&s_eaRewardItemList);

	if(info && info->pContactDialog)
	{
		for(i=0; i<eaSize(&info->pContactDialog->eaRewardBags); i++)
		{
			InventoryBag* pRewardBag = info->pContactDialog->eaRewardBags[i];
			// Interact type bags are choice bags (of which there can only be one per mission.  Display all other rewards in this list)
			if(pRewardBag->pRewardBagInfo->PickupType != kRewardPickupType_Choose)
				inv_bag_GetSimpleItemList(pRewardBag, &s_eaRewardItemList, false);
		}
	}

	for (i=0; i<eaSize(&s_eaRewardItemList); i++)
	{
		ItemDef *pItemDef = GET_REF(s_eaRewardItemList[i]->hItem);
		if (pItemDef && !!(pItemDef->flags & kItemDefFlag_Silent))
		{
			eaRemove(&s_eaRewardItemList, i);
			i--;
		}
	}
	ui_GenSetList(pGen, &s_eaRewardItemList, parse_Item);
}

static void contactUI_GetItemListForRewards(ContactDialog* pContactDialog, Item*** rewardItemList, bool bChoosable)
{
	int i;

	for(i=0; i<eaSize(&pContactDialog->eaRewardBags); i++)
	{
		InventoryBag* pRewardBag = pContactDialog->eaRewardBags[i];
		if((pRewardBag->pRewardBagInfo->PickupType == kRewardPickupType_Choose) == bChoosable
			&& (!bChoosable || (inv_bag_CountItems(pRewardBag, NULL) > 0 && pRewardBag->pRewardBagInfo->NumPicks > 0)))
		{
			inv_bag_GetSimpleItemList(pRewardBag, rewardItemList, false);
			if (bChoosable)
			{
				break;
			}
		}
	}
}

// This handles generating an arbitrary number of choosable item lists
static S32 contactUI_GetChoosableItemListForRewards(ContactDialog* pContactDialog,
													ChoosableItem*** peaChoosableItems)
{
	static Item** s_eaItems = NULL;
	S32 i, j, iCount = 0;

	for (i = 0; i < eaSize(&pContactDialog->eaRewardBags); i++)
	{
		InventoryBag* pRewardBag = pContactDialog->eaRewardBags[i];
		if (pRewardBag->pRewardBagInfo->PickupType == kRewardPickupType_Choose &&
			inv_bag_CountItems(pRewardBag, NULL) > 0 && pRewardBag->pRewardBagInfo->NumPicks > 0)
		{
			eaClear(&s_eaItems);
			inv_bag_GetSimpleItemList(pRewardBag, &s_eaItems, false);

			// Create the header
			if (eaSize(&s_eaItems) > 0)
			{
				ChoosableItem* pData = eaGetStruct(peaChoosableItems, parse_ChoosableItem, iCount++);
				REMOVE_HANDLE(pData->hItemDef);
				pData->pItem = NULL;
				pData->iBagIdx = i;
				pData->iNumPicks = pRewardBag->pRewardBagInfo->NumPicks;
				pData->bSelected = false;
			}
			// Fill the list of choosable items for this bag
			for (j = 0; j < eaSize(&s_eaItems); j++)
			{
				ChoosableItem* pData = eaGetStruct(peaChoosableItems, parse_ChoosableItem, iCount++);
				COPY_HANDLE(pData->hItemDef, s_eaItems[j]->hItem);
				pData->pItem = s_eaItems[j];
				pData->iBagIdx = i;
				pData->iNumPicks = pRewardBag->pRewardBagInfo->NumPicks;
				pData->bSelected = false;
			}
		}
	}
	return iCount;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogFillNonChoosableRewardListFiltered");
void exprContactDialogFillNonChoosableRewardListFiltered(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt, const char *pchIncludeDefs, const char *pchExcludeDefs)
{
	static Item** s_rewardItemList = NULL;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	InteractInfo *info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	S32 i;

	eaClear(&s_rewardItemList);

	if(info && info->pContactDialog)
	{
		contactUI_GetItemListForRewards(info->pContactDialog, &s_rewardItemList, false);
	}

	for (i=0; i<eaSize(&s_rewardItemList); i++)
	{
		ItemDef *pItemDef = GET_REF(s_rewardItemList[i]->hItem);
		if (pItemDef && !!(pItemDef->flags & kItemDefFlag_Silent))
		{
			eaRemove(&s_rewardItemList, i);
			i--;
		}
	}

	if (pchExcludeDefs && *pchExcludeDefs || pchIncludeDefs && *pchIncludeDefs)
	{
		bool bInclude = pchIncludeDefs != NULL && *pchIncludeDefs != 0;
		const char *pchFilterSet = bInclude ? pchIncludeDefs : pchExcludeDefs;
		U32 uLen = (U32)strlen(pchFilterSet) + 1;
		char *pchFilterDefs = _alloca(uLen);
		char *pchContext = NULL;
		char *pchToken;
		strcpy_s(pchFilterDefs, uLen, pchFilterSet);
		while ((pchToken = strtok_r(pchContext ? NULL : pchFilterDefs, " ,\r\n\t", &pchContext)) != NULL)
		{
			for (i=0; i<eaSize(&s_rewardItemList); i++)
			{
				ItemDef *pItemDef = GET_REF(s_rewardItemList[i]->hItem);
				if (pItemDef && (0 != stricmp(pItemDef->pchName, pchToken)) == bInclude)
				{
					eaRemove(&s_rewardItemList, i);
					i--;
				}
			}
		}
	}

	ui_GenSetList(pGen, &s_rewardItemList, parse_Item);
}

// Get a list of currently available choosable loot.  The player can pick some number of these.
// Assumes there is only one choosable reward bag per mission
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogFillChoosableRewardList");
void exprContactDialogFillChoosableRewardList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	static Item** s_rewardItemList = NULL;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	InteractInfo *info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);

	eaClear(&s_rewardItemList);

	if(info && info->pContactDialog)
	{
		contactUI_GetItemListForRewards(info->pContactDialog, &s_rewardItemList, true);
	}

	ui_GenSetList(pGen, &s_rewardItemList, parse_Item);
}

static void gclContactUI_GetSelectedChoosableItems(ChoosableItem ***peaData, ItemDef ***peaItemsDefs)
{
	S32 i;
	for (i = 0; i < eaSize(peaData); i++)
	{
		ChoosableItem* pData = (*peaData)[i];
		ItemDef* pItemDef = GET_REF(pData->hItemDef);
		if (pData->bSelected && pItemDef)
		{
			eaPush(peaItemsDefs, pItemDef);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ChoosableListGetSelectedItemsAsString");
const char* exprChoosableListGetSelectedItemsAsString(ExprContext *pContext, SA_PARAM_NN_VALID UIGen* pGen)
{
	ParseTable *pTable;
	ChoosableItem ***peaData = (ChoosableItem ***)ui_GenGetList(pGen, NULL, &pTable);
	static ItemDef** s_eaChosenItemDefs = NULL;
	static char* s_estrResult = NULL;
	const char* pchSelectedString = NULL;
	S32 i;

	estrClear(&s_estrResult);
	eaClearFast(&s_eaChosenItemDefs);

	if (pTable == parse_ChoosableItem)
	{
		gclContactUI_GetSelectedChoosableItems(peaData, &s_eaChosenItemDefs);
		for (i = 0; i < eaSize(&s_eaChosenItemDefs); i++)
		{
			if (estrLength(&s_estrResult) > 0)
			{
				estrAppend2(&s_estrResult, " ");
			}
			estrAppend2(&s_estrResult, s_eaChosenItemDefs[i]->pchName);
		}
	}

	return exprContextAllocString(pContext, s_estrResult);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ChoosableListSelectItem");
void exprChoosableListSelectItem(SA_PARAM_NN_VALID UIGen* pGen, SA_PARAM_OP_VALID ChoosableItem* pData)
{
	ParseTable *pTable;
	ChoosableItem ***peaData = (ChoosableItem ***)ui_GenGetList(pGen, NULL, &pTable);
	S32 i, iNumSelected = 1;

	//
	// FIXME(jm): This changes the contents of the list data.
	// Changing the contents of the list bad.
	//

	if (pTable == parse_ChoosableItem)
	{
		for (i = 0; i < eaSize(peaData); i++)
		{
			ChoosableItem* pCurData = (*peaData)[i];
			if (pData == pCurData)
			{
				pCurData->bSelected = true;
			}
			else if (pData->iBagIdx == pCurData->iBagIdx && pCurData->bSelected)
			{
				if (iNumSelected == pData->iNumPicks)
				{
					pCurData->bSelected = false;
				}
				else
				{
					iNumSelected++;
				}
			}
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ChoosableListHasSelectedAllItems");
bool exprChoosableListHasSelectedAllItems(SA_PARAM_NN_VALID UIGen* pGen)
{
	ParseTable *pTable;
	ChoosableItem ***peaData = (ChoosableItem ***)ui_GenGetList(pGen, NULL, &pTable);
	S32 i;
	S32 iNumSelected = 0;
	S32 iReqSelected = 0;

	if (pTable == parse_ChoosableItem)
	{
		for (i = 0; i < eaSize(peaData); i++)
		{
			ChoosableItem* pData = (*peaData)[i];
			if (pData->pItem)
			{
				if (pData->bSelected)
				{
					iNumSelected++;
				}
			}
			else
			{
				iReqSelected += pData->iNumPicks;
			}
		}
	}
	return (iNumSelected == iReqSelected);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogFillCategorizedChoosableRewardList");
void exprContactDialogFillCategorizedChoosableRewardList(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt)
{
	static ItemDef** s_eaChosenItemDefs = NULL;
	UIGen *pGen = exprContextGetUserPtr(pContext, parse_UIGen);
	ChoosableItem ***peaData = ui_GenGetManagedListSafe(pGen, ChoosableItem);
	InteractInfo *pInfo = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	S32 i;

	eaClearFast(&s_eaChosenItemDefs);
	gclContactUI_GetSelectedChoosableItems(peaData, &s_eaChosenItemDefs);

	if (SAFE_MEMBER(pInfo, pContactDialog))
	{
		S32 iCount = contactUI_GetChoosableItemListForRewards(pInfo->pContactDialog, peaData);
		eaSetSizeStruct(peaData, parse_ChoosableItem, iCount);
	}
	else
	{
		eaClearStruct(peaData, parse_ChoosableItem);
	}
	for (i = eaSize(peaData)-1; i >= 0; i--)
	{
		ItemDef* pItemDef = GET_REF((*peaData)[i]->hItemDef);
		if (pItemDef && eaFindAndRemove(&s_eaChosenItemDefs, pItemDef) >= 0)
		{
			(*peaData)[i]->bSelected = true;
		}
	}
	ui_GenSetManagedListSafe(pGen, peaData, ChoosableItem, true);
}

static void contactUI_SMFFromItemList(Item ***itemList, char** pchResult, const char *pchRowFormat, const char *pchRowFormatWithValue)
{
	int i;

	for (i = 0; i < eaSize(itemList); i++)
	{
		Item *pItem = (*itemList)[i];
		ItemDef *pItemDef = pItem ? GET_REF(pItem->hItem) : NULL;

		if(pItemDef && !(pItemDef->flags & kItemDefFlag_Silent))
		{
			FormatGameString(pchResult, pItem->count ? pchRowFormatWithValue : pchRowFormat, STRFMT_ITEM(pItem), STRFMT_END);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetRewardCount");
int exprContactMissionRewardCount(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt, const char *pchResources)
{
	InteractInfo *info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	static Item **ppRewardItemList = NULL;
	ItemDef *pResources = item_DefFromName(pchResources);
	S32 i;
	S32 iTotalCount = 0;
	eaClear(&ppRewardItemList);

	if (pResources && pResources->eType == kItemType_Numeric)
	{
		if(info && info->pContactDialog && pResources)
		{
			contactUI_GetItemListForRewards(info->pContactDialog, &ppRewardItemList, false);
		}

		for (i=0; i<eaSize(&ppRewardItemList); i++)
		{
			ItemDef *pItemDef = GET_REF(ppRewardItemList[i]->hItem);
			if (pItemDef == pResources)
			{
				iTotalCount += ppRewardItemList[i]->count;
			}
		}
	}

	return iTotalCount;
}

// Generate SMF for rewards for this mission.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetRewardStringFiltered");
const char *exprContactMissionRewardStringFiltered(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt, const char *pchRowFormat, const char *pchRowFormatWithValue, const char *pchIncludeDefs, const char *pchExcludeDefs)
{
	InteractInfo *info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	static Item **ppRewardItemList = NULL;
	char *pchResult = NULL;
	char *pchReturn;
	size_t sz;
	S32 i;
	estrStackCreate(&pchResult);
	eaClear(&ppRewardItemList);

	if(info && info->pContactDialog)
	{
		contactUI_GetItemListForRewards(info->pContactDialog, &ppRewardItemList, false);
	}

	for (i=0; i<eaSize(&ppRewardItemList); i++)
	{
		ItemDef *pItemDef = GET_REF(ppRewardItemList[i]->hItem);
		if (pItemDef && !!(pItemDef->flags & kItemDefFlag_Silent))
		{
			eaRemove(&ppRewardItemList, i);
			i--;
		}
	}

	if (pchExcludeDefs && *pchExcludeDefs || pchIncludeDefs && *pchIncludeDefs)
	{
		bool bInclude = pchIncludeDefs != NULL && *pchIncludeDefs != 0;
		const char *pchFilterSet = bInclude ? pchIncludeDefs : pchExcludeDefs;
		U32 uLen = (U32)strlen(pchFilterSet) + 1;
		char *pchFilterDefs = _alloca(uLen);
		char *pchContext = NULL;
		char *pchToken;
		strcpy_s(pchFilterDefs, uLen, pchFilterSet);
		while ((pchToken = strtok_r(pchContext ? NULL : pchFilterDefs, " ,\r\n\t", &pchContext)) != NULL)
		{
			for (i=0; i<eaSize(&ppRewardItemList); i++)
			{
				ItemDef *pItemDef = GET_REF(ppRewardItemList[i]->hItem);
				if (pItemDef && (0 != stricmp(pItemDef->pchName, pchToken)) == bInclude)
				{
					eaRemove(&ppRewardItemList, i);
					i--;
				}
			}
		}
	}

	contactUI_SMFFromItemList(&ppRewardItemList, &pchResult, pchRowFormat, pchRowFormatWithValue);

	sz = strlen(pchResult) + 1;
	pchReturn = exprContextAllocScratchMemory(pContext, sz);
	strcpy_s(pchReturn, sz, pchResult);
	estrDestroy(&pchResult);
	return pchReturn;
}

// Generate SMF for rewards for this mission.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetRewardString");
const char *exprContactMissionRewardString(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt, const char *pchRowFormat, const char *pchRowFormatWithValue)
{
	return exprContactMissionRewardStringFiltered(pContext, pPlayerEnt, pchRowFormat, pchRowFormatWithValue, NULL, NULL);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactGetBackOption");
S32 exprContactGetBackOption(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog){
		int i;
		for (i = eaSize(&pDialog->eaOptions)-1; i >= 0; --i){
			if (pDialog->eaOptions[i]->bIsDefaultBackOption){
				return i;
			}
		}
	}
	return -1;
}

// Generate SMF for rewards for this mission.
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetChoosableRewardString");
const char *exprContactMissionChoosableRewardString(ExprContext *pContext, SA_PARAM_OP_VALID Entity *pPlayerEnt, const char *pchRowFormat, const char *pchRowFormatWithValue)
{
	Item **ppRewardItemList = NULL;
	char *pchResult = NULL;
	char *pchReturn;
	size_t sz;
	InteractInfo *info = SAFE_MEMBER2(pPlayerEnt, pPlayer, pInteractInfo);
	estrStackCreate(&pchResult);

	if(info && info->pContactDialog)
	{
		contactUI_GetItemListForRewards(info->pContactDialog, &ppRewardItemList, true);
	}

	contactUI_SMFFromItemList(&ppRewardItemList, &pchResult, pchRowFormat, pchRowFormatWithValue);

	sz = strlen(pchResult) + 1;
	pchReturn = exprContextAllocScratchMemory(pContext, sz);
	strcpy_s(pchReturn, sz, pchResult);
	estrDestroy(&pchResult);
	eaDestroy(&ppRewardItemList);
	return pchReturn;
}

/*******************************
 *
 *	Remote Contact Expressions
 *
 *******************************/

/*
 *	HELPER FUNCTIONS
 */

static RemoteContact* remoteContact_GetRemoteContactFromDisplayRow(const RemoteContactDisplayRow *pDisplayRow)
{
	if(pDisplayRow && pDisplayRow->pchContactName && geaCachedRemoteContacts)
	{
		return eaIndexedGetUsingString(&geaCachedRemoteContacts, pDisplayRow->pchContactName);
	}

	return NULL;
}

static RemoteContactOption* remoteContact_GetRemoteOptionFromDisplayRow(const RemoteContactDisplayRow *pDisplayRow)
{
	RemoteContact* pContact = pDisplayRow ? remoteContact_GetRemoteContactFromDisplayRow(pDisplayRow) : NULL;

	if(pDisplayRow && pDisplayRow->pchOptionKey && pContact && pContact->eaOptions)
	{
		return eaIndexedGetUsingString(&pContact->eaOptions, pDisplayRow->pchOptionKey);
	}

	return NULL;
}

static const char* remoteContact_GetOptionDisplayName(const RemoteContactOption* pOption, const char* pchDefault)
{
	const char* pchName = NULL;
	if(pOption)
	{
		if(GET_REF(pOption->hMissionDisplayName))
		{
			pchName = TranslateMessageRef(pOption->hMissionDisplayName);
		}
		else if(pOption->pOption && pOption->pOption->pchDisplayString)
		{
			pchName = pOption->pOption->pchDisplayString;
		}
	}
	else if(pchDefault)
	{
		pchName = pchDefault;
	}

	return NULL_TO_EMPTY(pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GetContactOptionFromRemoteDisplayRow);
SA_RET_OP_VALID ContactDialogOption* exprGetContactOptionFromRemoteDisplayRow(SA_PARAM_OP_VALID RemoteContactDisplayRow *pDisplayRow)
{
	RemoteContactOption* pRemoteOption = remoteContact_GetRemoteOptionFromDisplayRow(pDisplayRow);
	return pRemoteOption ? pRemoteOption->pOption : NULL;
}

/*
 *	SORTING
 */

static int remoteContactOption_CompareDisplayNames(const RemoteContactOption* pOptionA,
												   const RemoteContactOption* pOptionB,
												   const RemoteContactDisplayRow* pRowA,
												   const RemoteContactDisplayRow* pRowB)
{
	const char* pchHeaderA = SAFE_MEMBER(pRowA, pchHeaderDisplayString);
	const char* pchHeaderB = SAFE_MEMBER(pRowB, pchHeaderDisplayString);
	const char* pchDisplayStringA = remoteContact_GetOptionDisplayName(pOptionA, pchHeaderA);
	const char* pchDisplayStringB = remoteContact_GetOptionDisplayName(pOptionB, pchHeaderB);
	return strcmp_safe(pchDisplayStringA, pchDisplayStringB);
}

static int remoteContactDisplayRow_Compare_OptionIndicator(const RemoteContactDisplayRow** a, const RemoteContactDisplayRow** b)
{
	int iCmp;
	RemoteContactOption* pOptionA;
	RemoteContactOption* pOptionB;

	if(!a || !b || !(*a) || !(*b) || !(*a)->pchContactName || !(*b)->pchContactName || !(*a)->pchOptionKey || !(*b)->pchOptionKey)
	{
		return 0;
	}

	pOptionA = remoteContact_GetRemoteOptionFromDisplayRow((*a));
	pOptionB = remoteContact_GetRemoteOptionFromDisplayRow((*b));

	if(!pOptionA || !pOptionA->pOption || !pOptionB || !pOptionB->pOption)
	{
		return 0;
	}

	iCmp = (pOptionB->pOption->eType - pOptionA->pOption->eType);
	if (!iCmp)
	{
		return remoteContactOption_CompareDisplayNames(pOptionA, pOptionB, (*a), (*b));
	}
	return iCmp;
}

static int remoteContactDisplayRow_Compare_ContactName(const RemoteContactDisplayRow** a, const RemoteContactDisplayRow** b)
{
	RemoteContact* pContactA;
	RemoteContact* pContactB;
	const char* pchDisplayNameA;
	const char* pchDisplayNameB;

	if(!a || !b || !(*a) || !(*b) || !(*a)->pchContactName || !(*b)->pchContactName || !(*a)->pchOptionKey || !(*b)->pchOptionKey)
	{
		return 0;
	}

	pContactA = remoteContact_GetRemoteContactFromDisplayRow((*a));
	pContactB = remoteContact_GetRemoteContactFromDisplayRow((*b));

	if(!pContactA || !pContactB)
	{
		return 0;
	}

	pchDisplayNameA = TranslateMessageRef(pContactA->hDisplayNameMsg);
	pchDisplayNameB = TranslateMessageRef(pContactB->hDisplayNameMsg);

	return(strcmp_safe(pchDisplayNameA, pchDisplayNameB));
}

static int remoteContactDisplayRow_Compare_NewEx(const RemoteContactDisplayRow** a,
												 const RemoteContactDisplayRow** b,
												 bool bCompareDisplayStrings)
{
	int iCmp;
	RemoteContactOption* pOptionA;
	RemoteContactOption* pOptionB;

	if(!a || !b || !(*a) || !(*b) || !(*a)->pchContactName || !(*b)->pchContactName || !(*a)->pchOptionKey || !(*b)->pchOptionKey)
	{
		return 0;
	}

	pOptionA = remoteContact_GetRemoteOptionFromDisplayRow((*a));
	pOptionB = remoteContact_GetRemoteOptionFromDisplayRow((*b));

	if(!pOptionA || !pOptionB)
	{
		return 0;
	}

	iCmp = (pOptionB->bNew - pOptionA->bNew);
	if (!iCmp && bCompareDisplayStrings)
	{
		return remoteContactOption_CompareDisplayNames(pOptionA, pOptionB, (*a), (*b));
	}
	return iCmp;
}

static int remoteContactDisplayRow_Compare_New(const RemoteContactDisplayRow** a, const RemoteContactDisplayRow** b)
{
	return remoteContactDisplayRow_Compare_NewEx(a, b, true);
}

static int remoteContactDisplayRow_Compare_MissionCategory(const RemoteContactDisplayRow** a, const RemoteContactDisplayRow** b)
{
	int iCmp;
	RemoteContactOption* pOptionA;
	RemoteContactOption* pOptionB;
	const char* pchCategoryA = NULL;
	const char* pchCategoryB = NULL;

	if(!a || !b || !(*a) || !(*b) || !(*a)->pchContactName || !(*b)->pchContactName || !(*a)->pchOptionKey || !(*b)->pchOptionKey)
	{
		return 0;
	}

	pOptionA = remoteContact_GetRemoteOptionFromDisplayRow((*a));
	pOptionB = remoteContact_GetRemoteOptionFromDisplayRow((*b));

	if(!pOptionA || !pOptionB)
	{
		return 0;
	}

	if (GET_REF(pOptionA->hMissionCategory)) {
		pchCategoryA = TranslateDisplayMessage(GET_REF(pOptionA->hMissionCategory)->displayNameMsg);
	}
	if (GET_REF(pOptionB->hMissionCategory)) {
		pchCategoryB = TranslateDisplayMessage(GET_REF(pOptionB->hMissionCategory)->displayNameMsg);
	}

	iCmp = (strcmp_safe(pchCategoryA, pchCategoryB));
	if (!iCmp)
	{
		return remoteContactOption_CompareDisplayNames(pOptionA, pOptionB, (*a), (*b));
	}
	return iCmp;
}

// Compares two remote contact display rows based on newness then on type then on contact display name (alphabetically)
static int remoteContactDisplayRow_Compare_Default(const RemoteContactDisplayRow** a, const RemoteContactDisplayRow** b)
{
	int iReturn;
	if(!a || !b || !(*a) || !(*b)) {
		return 0;
	}

	iReturn = remoteContactDisplayRow_Compare_ContactName(a,b);
	if (iReturn == 0)
	{
		iReturn = (!!(*b)->bIsHeader) - (!!(*a)->bIsHeader);
		if(iReturn == 0)
		{
			iReturn = remoteContactDisplayRow_Compare_NewEx(a,b,false);
			if(iReturn == 0)
			{
				iReturn = remoteContactDisplayRow_Compare_OptionIndicator(a,b);
			}
		}
	}

	return iReturn;
}

static RemoteContact* remoteContact_FindContactWithOption(	Entity *pPlayerEnt, 
															const char *pchContactName, 
															const char *pchOptionKey, 
															RemoteContactOption **ppOptionOut)
{
	InteractInfo* pInfo = pPlayerEnt && pPlayerEnt->pPlayer ? pPlayerEnt->pPlayer->pInteractInfo : NULL;
	RemoteContact** eaServerList = pInfo ? pInfo->eaRemoteContacts : NULL;
	
	if (!eaServerList)
		return NULL;

	FOR_EACH_IN_EARRAY(eaServerList, RemoteContact, pRemoteContact)
	{
		if (!stricmp(pRemoteContact->pchContactDef,pchContactName))
		{
			FOR_EACH_IN_EARRAY(pRemoteContact->eaOptions, RemoteContactOption, pOption)
			{
				if (!stricmp(pOption->pchKey, pchOptionKey))
				{
					if (ppOptionOut) 
						*ppOptionOut = pOption;

					return pRemoteContact;
				}
			}
			FOR_EACH_END

			return NULL;
		}
	}
	FOR_EACH_END

	return NULL;
}

/*
 *	DISPLAY
 */

// Returns the remote contact's translated display name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRemoteContactName);
const char* exprGenGetRemoteContactName(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pPlayerEnt, SA_PARAM_NN_VALID RemoteContact* pRContact) {
	static char* s_estrResult = NULL;

	if (pRContact->estrFormattedContactName)
	{
		return pRContact->estrFormattedContactName;
	}

	estrClear(&s_estrResult);
	langFormatGameMessage(entGetLanguage(pPlayerEnt), &s_estrResult, GET_REF(pRContact->hDisplayNameMsg), STRFMT_PLAYER(pPlayerEnt), STRFMT_END);
	return s_estrResult && *s_estrResult ? exprContextAllocString(pContext, s_estrResult) : "";
}

// Initiates dialog with the remote contact
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenShowRemoteContact);
void exprGenShowRemoteContact(ExprContext *pContext, SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID RemoteContact* pRContact) {
	InteractInfo* pInfo= SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);

	if(pRContact->pchContactDef && pInfo && pInfo->eaRemoteContacts && eaFindCmp(&pInfo->eaRemoteContacts, pRContact, remoteContact_CompareNames) >= 0) {
		pRContact->bIsNew = false;
		ServerCmd_contact_StartRemoteContact(pRContact->pchContactDef);
	}
}

// Initiates dialog with the remote contact
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenShowRemoteContactByName);
void exprGenShowRemoteContactByName(ExprContext *pContext, SA_PARAM_NN_VALID Entity* pEnt, SA_PARAM_NN_VALID const char *chName) {
	ServerCmd_contact_StartRemoteContact(chName);
}

// A non-zero return indicates the contact is capable of granting a mission remotely
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRemoteContactGrantFlag);
int exprGenGetRemoteGrantFlag(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContact* pRContact)
{
	return (pRContact->eFlags & ContactFlag_RemoteOfferGrant);
}

// A non-zero return indicates the contact is capable of accepting a mission turn-in remotely
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRemoteContactReturnFlag);
int exprGenGetRemoteReturnFlag(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContact* pRContact)
{
	return (pRContact->eFlags & ContactFlag_RemoteOfferReturn);
}

// A non-zero return indicates the contact is capable of accepting a mission turn-in remotely
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRemoteContactInProgressFlag);
int exprGenGetRemoteInProgressFlag(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContact* pRContact)
{
	return (pRContact->eFlags & ContactFlag_RemoteOfferInProgress);
}

// A non-zero return indicates the contact is capable of displaying a special dialog block remotely
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRemoteContactSpecDialogFlag);
int exprGenGetRemoteSpecDialogFlag(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContact* pRContact)
{
	return (pRContact->eFlags & ContactFlag_RemoteSpecDialog);
}

// A non-zero return indicates the contact has not been remotely contacted by the player
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRemoteContactNewFlag);
int exprGenGetRemoteNewFlag(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContact* pRContact)
{
	return pRContact->bIsNew;
}

// Returns the display name of the contact associated with the remote contact display row
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_GetContactName);
const char* exprRemoteContactDisplayRow_GetContactName(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	Entity *pPlayer = entActivePlayerPtr();
	RemoteContact* pContact = remoteContact_GetRemoteContactFromDisplayRow(pRow);
	if (pContact && pPlayer)
	{
		ANALYSIS_ASSUME(pContact);
		ANALYSIS_ASSUME(pPlayer);
		return exprGenGetRemoteContactName(pContext, pPlayer, pContact);
	}
	else
	{
		return "";
	}
}

// Takes a remote contact display row and returns either the mission name (if the option is associated with a mission)
// or the display string for the option.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_GetOptionName);
const char* exprRemoteContactDisplayRow_GetOptionName(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	RemoteContactOption* pOption = remoteContact_GetRemoteOptionFromDisplayRow(pRow);

	return remoteContact_GetOptionDisplayName(pOption, pRow->pchHeaderDisplayString);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_GetOptionMissionName);
const char* exprRemoteContactDisplayRow_GetOptionMissionName(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	RemoteContactOption* pOption = remoteContact_GetRemoteOptionFromDisplayRow(pRow);

	if (pOption)
	{
		return pOption->pcMissionName;
	}
	return "";
}

// Returns true if the remote contact display row is flagged as new
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_IsNew);
bool exprRemoteContactDisplayRow_IsNew(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	RemoteContactOption* pOption = remoteContact_GetRemoteOptionFromDisplayRow(pRow);
	if(pOption)
	{
		return pOption->bNew;
	}
	return false;
}

// Sets the "isNew" field for the display row
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_SetIsNew);
void exprRemoteContactDisplayRow_SetIsNew(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow, bool bIsNew)
{
	RemoteContactOption* pOption = remoteContact_GetRemoteOptionFromDisplayRow(pRow);
	if(pOption)
	{
		pOption->bNew = bIsNew;
	}
}

// Returns the indicator associated with the remote contact option
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_GetOptionIndicator);
const char* exprRemoteContactDisplayRow_GetOptionIndicator(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	RemoteContactOption* pOption = remoteContact_GetRemoteOptionFromDisplayRow(pRow);
	if(pOption && pOption->pOption)
	{
		return StaticDefineIntRevLookup(ContactIndicatorEnum, pOption->pOption->eType);
	}
	return "NoInfo";
}

// Returns the row type (Normal, UGC, or Header) of the RemoteContactDisplayRow
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_GetRowType);
const char* exprRemoteContactDisplayRow_GetRowType(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	RemoteContactOption* pOption = remoteContact_GetRemoteOptionFromDisplayRow(pRow);
	if(pOption)
	{
		if(pOption->pcMissionName)
		{
			if(gclUGC_IsResourceUGC(pOption->pcMissionName))
				return "UGC";
		}
	}
	else if (pRow->bIsHeader)
	{
		return "Header";
	}

	return "Normal";
}

// Returns the translated mission category of the mission associated with the RemoteContactDisplayRow
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_GetMissionCategory);
const char* exprRemoteContactDisplayRow_GetMissionCategory(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	RemoteContactOption* pOption = remoteContact_GetRemoteOptionFromDisplayRow(pRow);
	if(pOption)
	{
		if(GET_REF(pOption->hMissionCategory))
		{
			return TranslateDisplayMessage(GET_REF(pOption->hMissionCategory)->displayNameMsg);
		}
	}
	return "";
}

// Returns the translated description associated with the remote contact option
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_GetOptionDescriptionText1);
const char* exprRemoteContactDisplayRow_GetOptionDescriptionText1(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	RemoteContactOption* pOption = remoteContact_GetRemoteOptionFromDisplayRow(pRow);
	if(pOption)
	{
		if(pOption->pchDescription1)
			return pOption->pchDescription1;
		else if(!pOption->bDescriptionRequested && !pOption->pchDescription2)
		{
			pOption->bDescriptionRequested = true;
			ServerCmd_RemoteContactOption_RequestDescription(pRow->pchContactName, pRow->pchOptionKey);
		}
	}
	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_GetOptionDescriptionText2);
const char* exprRemoteContactDisplayRow_GetOptionDescriptionText2(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	RemoteContactOption* pOption = remoteContact_GetRemoteOptionFromDisplayRow(pRow);
	if(pOption)
	{
		if(pOption->pchDescription2)
			return pOption->pchDescription2;
		else if(!pOption->bDescriptionRequested && !pOption->pchDescription1)
		{
			pOption->bDescriptionRequested = true;
			ServerCmd_RemoteContactOption_RequestDescription(pRow->pchContactName, pRow->pchOptionKey);
		}
	}
	return "";
}

// DEPRECATED; Use UIGenPaperdoll
AUTO_EXPR_FUNC(UIGen) ACMD_NAME("RemoteContactDisplayRow_GetHeadshot");
SA_RET_OP_VALID BasicTexture *exprRemoteContactDisplayRow_GetHeadshot( SA_PARAM_OP_VALID const char* pchContact,
																    SA_PARAM_OP_VALID const char* pchOptionKey,
																	SA_PARAM_OP_VALID Entity *pPlayer,
																	SA_PARAM_OP_VALID BasicTexture *pTexture,
																	F32 fWidth, F32 fHeight)
{
	return NULL;
}

// Starts the contact using the contact and option information stored in the specified row
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContactDisplayRow_LaunchContactFromRow);
void exprRemoteContactDisplayRow_LaunchContactFromRow(ExprContext *pContext, SA_PARAM_NN_VALID RemoteContactDisplayRow* pRow)
{
	if(pRow && pRow->pchContactName)
	{
		if(pRow->pchOptionKey)
			ServerCmd_contact_StartRemoteContactWithOption(pRow->pchContactName, pRow->pchOptionKey);
		else
			ServerCmd_contact_StartRemoteContact(pRow->pchContactName);

		exprRemoteContactDisplayRow_SetIsNew(pContext, pRow, false);
	}
}

// Attempts to start the contact using the given contact and option key name
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(RemoteContact_LaunchContact);
void exprRemoteContact_LaunchContact(	SA_PARAM_NN_VALID const char *pchContactName, 
										SA_PARAM_NN_VALID const char *pchOptionKey)
{
	if(pchContactName && pchOptionKey)
	{
		Entity* pEnt = entActivePlayerPtr();
		RemoteContactOption *pOption = NULL;
		RemoteContact *pContact = remoteContact_FindContactWithOption(pEnt, pchContactName, pchOptionKey, &pOption);

		if (pEnt->pPlayer && pEnt->pPlayer->pInteractInfo && pEnt->pPlayer->pInteractInfo->pContactDialog)
		{	// we are already interacting, do not allow this one
			return;
		}

		if (pContact && pOption)
		{
			ServerCmd_contact_StartRemoteContactWithOption(pContact->pchContactDef, pOption->pchKey);
		}
	}
}


// Compares the entity's remote contact list to the cached client copy.  If there are any differences, a new copy is cached.
// If there are any new contacts added to the list, the function returns true
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenNewRemoteContacts);
bool exprGenNewRemoteContacts(ExprContext *pContext, SA_PARAM_OP_VALID Entity* pEnt)
{
	return !sbRemoteContactsViewed;
}

// Sets the remote contact list as being viewed so that it doesn't pop up as having new contacts
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRemoteContactListViewed);
void exprRemoteContactListViewed( SA_PARAM_NN_VALID UIGen *pGen) {
	sbRemoteContactsViewed = true;

}

// Sets the UIGen's list to the client's cached remote contact list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetRemoteContactList);
void exprGenGetRemoteContactList( SA_PARAM_NN_VALID UIGen *pGen) {
	Entity* pEnt = entActivePlayerPtr();
	Player* pPlayer = pEnt? pEnt->pPlayer : NULL;
	InteractInfo* pInfo = pPlayer? pPlayer->pInteractInfo : NULL;
	RemoteContact** eaServerList = pInfo ? pInfo->eaRemoteContacts : NULL;
	bool newContacts;			//True if new contacts added to the list
	bool ContactsNotVisited;	//True if contacts exist which haven't been visited
	int i, idx;

	if(!eaServerList) {
		ui_GenSetList(pGen, NULL, parse_RemoteContact);
		return;
	}

	if(!geaCachedRemoteContacts) {
		eaCreate(&geaCachedRemoteContacts);
	}

	newContacts = false;
	ContactsNotVisited = false;

	for(i = eaSize(&eaServerList)-1; i >= 0; i--) {
		idx = eaFindCmp(&geaCachedRemoteContacts, eaServerList[i], remoteContact_CompareNames);
		if(idx >= 0) {
			ContactsNotVisited = ContactsNotVisited && geaCachedRemoteContacts[idx]->bIsNew;
			eaServerList[i]->bIsNew = geaCachedRemoteContacts[idx]->bIsNew;
			geaCachedRemoteContacts[idx]->eFlags = eaServerList[i]->eFlags;
		} else {
			eaServerList[i]->bIsNew = true;
			newContacts = true;
			ContactsNotVisited = true;
		}
	}

	if(newContacts || eaSize(&eaServerList) != eaSize(&geaCachedRemoteContacts)) {
		eaCopyStructs(&eaServerList, &geaCachedRemoteContacts, parse_RemoteContact);
	}

	eaQSort(geaCachedRemoteContacts, remoteContact_Compare);

	//Contacts viewed if previously viewed or at least one contact has been visited and there are no new contacts.
	sbRemoteContactsViewed = sbRemoteContactsViewed || (!ContactsNotVisited && !newContacts);

	ui_GenSetList(pGen, &geaCachedRemoteContacts, parse_RemoteContact);
}

// Assumes the passed list is already sorted
static void RemoteContactDisplayRow_InsertHeaders(RemoteContactDisplayRow*** peaList)
{
	S32 i;
	const char* pchLastContactName = NULL;
	for (i = 0; i < eaSize(peaList); i++)
	{
		RemoteContactDisplayRow* pRow = (*peaList)[i];
		if (pchLastContactName != pRow->pchContactName)
		{
			if (!pRow->bIsHeader)
			{
				RemoteContactDisplayRow* pHeader = StructCreate(parse_RemoteContactDisplayRow);
				pHeader->pchContactName = allocAddString(pRow->pchContactName);
				pHeader->bIsHeader = true;
				eaInsert(peaList, pHeader, i++);
			}
			pchLastContactName = pRow->pchContactName;
		}
	}
}

static void gclGenGetExpandedRemoteContactList(SA_PARAM_NN_VALID UIGen* pGen, bool bInsertContactHeaders)
{
	Entity* pEnt = entActivePlayerPtr();
	Player* pPlayer = pEnt? pEnt->pPlayer : NULL;
	InteractInfo* pInfo = pPlayer? pPlayer->pInteractInfo : NULL;
	RemoteContact** eaServerList = pInfo ? pInfo->eaRemoteContacts : NULL;
	RemoteContactDisplayRow*** peaRemoteContactDisplayRows = ui_GenGetManagedListSafe(pGen, RemoteContactDisplayRow);
	bool bNewContacts;			//True if new contacts added to the list
	bool bContactsNotVisited;	//True if contacts exist which haven't been visited
	int i, j, iOption, idx;

	if(!eaServerList) {
		ui_GenSetList(pGen, NULL, parse_RemoteContact);
		return;
	}

	if(!geaCachedRemoteContacts) {
		eaCreate(&geaCachedRemoteContacts);
		eaIndexedEnable(&geaCachedRemoteContacts, parse_RemoteContact);
	}

	bNewContacts = false;
	bContactsNotVisited = false;

	for(i = eaSize(&eaServerList)-1; i >= 0; i--) {
		idx = eaIndexedFindUsingString(&geaCachedRemoteContacts, eaServerList[i]->pchContactDef);
		if(idx >= 0) {
			bContactsNotVisited = bContactsNotVisited && geaCachedRemoteContacts[idx]->bIsNew;
			eaServerList[i]->bIsNew = geaCachedRemoteContacts[idx]->bIsNew;
			eaServerList[i]->bHeadshotRequested = geaCachedRemoteContacts[idx]->bHeadshotRequested;
			geaCachedRemoteContacts[idx]->eFlags = eaServerList[i]->eFlags;
			if(eaServerList[i]->pHeadshot && !geaCachedRemoteContacts[idx]->pHeadshot)
			{
				geaCachedRemoteContacts[idx]->pHeadshot = StructClone(parse_ContactHeadshotData, eaServerList[i]->pHeadshot);
			}

			if (eaServerList[i]->estrFormattedContactName)
				estrCopy(&geaCachedRemoteContacts[idx]->estrFormattedContactName, &eaServerList[i]->estrFormattedContactName);
			else if (geaCachedRemoteContacts[idx]->estrFormattedContactName)
				estrDestroy(&geaCachedRemoteContacts[idx]->estrFormattedContactName);

			for (j=0; j < eaSize(&eaServerList[i]->eaOptions); j++)
			{
				RemoteContactOption* pOption = eaServerList[i]->eaOptions[j];
				iOption = eaIndexedFindUsingString(&geaCachedRemoteContacts[idx]->eaOptions, pOption->pchKey);
				if(iOption < 0)
				{
					pOption->bNew = true;
					bContactsNotVisited = true;
					bNewContacts = true;
				}
				else
				{
					bContactsNotVisited = bContactsNotVisited && geaCachedRemoteContacts[idx]->eaOptions[iOption]->bNew;
					pOption->bNew = geaCachedRemoteContacts[idx]->eaOptions[iOption]->bNew;
					pOption->bDescriptionRequested = geaCachedRemoteContacts[idx]->eaOptions[iOption]->bDescriptionRequested;
					if(pOption->pchDescription1)
						StructCopyString(&geaCachedRemoteContacts[idx]->eaOptions[iOption]->pchDescription1, pOption->pchDescription1);
					if(pOption->pchDescription2)
						StructCopyString(&geaCachedRemoteContacts[idx]->eaOptions[iOption]->pchDescription2, pOption->pchDescription2);
				}
			}
		} else {
			eaServerList[i]->bIsNew = true;
			for (j=0; j < eaSize(&eaServerList[i]->eaOptions); j++)
			{
				eaServerList[i]->eaOptions[j]->bNew = true;
			}
			bNewContacts = true;
			bContactsNotVisited = true;
		}
	}

	if(bNewContacts || eaSize(&eaServerList) != eaSize(&geaCachedRemoteContacts)) {
		eaClearStruct(&geaCachedRemoteContacts, parse_RemoteContact);
		for (i=0; i < eaSize(&eaServerList); i++)
		{
			RemoteContact* pServerContact = eaServerList[i];
			if (pServerContact->bHeadshotRequested && !pServerContact->pHeadshot)
			{
				pServerContact->bHeadshotRequested = false;
			}
			for (j = eaSize(&pServerContact->eaOptions)-1; j >= 0; j--)
			{
				RemoteContactOption* pOption = pServerContact->eaOptions[j];
				if (pOption->bDescriptionRequested && !pOption->pchDescription1 && !pOption->pchDescription2)
				{
					pOption->bDescriptionRequested = false;
				}
			}
			eaIndexedAdd(&geaCachedRemoteContacts, StructClone(parse_RemoteContact, pServerContact));
		}
	}

	iOption = 0;

	// Create display rows
	for (i=0; i < eaSize(&geaCachedRemoteContacts); i++)
	{
		RemoteContact* pRemoteContact = geaCachedRemoteContacts[i];
		for (j=0; j < eaSize(&pRemoteContact->eaOptions); j++)
		{
			RemoteContactOption* pOption = pRemoteContact->eaOptions[j];
			RemoteContactDisplayRow* pDisplayRow = eaGetStruct(peaRemoteContactDisplayRows, parse_RemoteContactDisplayRow, iOption++);
			pDisplayRow->pchContactName = allocAddString(pRemoteContact->pchContactDef);
			if ((!pDisplayRow->pchOptionKey && pOption->pchKey) || (pDisplayRow->pchOptionKey && !pOption->pchKey) || (pDisplayRow->pchOptionKey && stricmp(pDisplayRow->pchOptionKey, pOption->pchKey))) {
				StructCopyString(&pDisplayRow->pchOptionKey, pOption->pchKey);
			}
			if (pDisplayRow->pchHeaderDisplayString) {
				StructFreeStringSafe(&pDisplayRow->pchHeaderDisplayString);
			}
			pDisplayRow->bIsHeader = false;
		}
	}

	eaSetSizeStruct(peaRemoteContactDisplayRows, parse_RemoteContactDisplayRow, iOption);
	eaQSort(*peaRemoteContactDisplayRows, remoteContactDisplayRow_Compare_Default);

	if (bInsertContactHeaders)
	{
		RemoteContactDisplayRow_InsertHeaders(peaRemoteContactDisplayRows);
	}

	//Contacts viewed if previously viewed or at least one contact has been visited and there are no new contacts.
	sbRemoteContactsViewed = sbRemoteContactsViewed || (!bContactsNotVisited && !bNewContacts);

	if(!geaCachedRemoteContacts || !eaSize(&geaCachedRemoteContacts))
	{
		ui_GenSetListSafe(pGen, NULL, RemoteContactDisplayRow);
	}
	else
	{
		ui_GenSetManagedListSafe(pGen, peaRemoteContactDisplayRows, RemoteContactDisplayRow, true);
	}
}


// Sets the UIGen's list to the full list of remote contact options
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetExpandedRemoteContactListWithHeaders);
void exprGenGetExpandedRemoteContactListWithHeaders(SA_PARAM_NN_VALID UIGen *pGen)
{
	gclGenGetExpandedRemoteContactList(pGen, true);
}

// Sets the UIGen's list to the full list of remote contact options
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetExpandedRemoteContactList);
void exprGenGetExpandedRemoteContactList(SA_PARAM_NN_VALID UIGen *pGen)
{
	gclGenGetExpandedRemoteContactList(pGen, false);
}

// Sorts the gen's expanded remote contact list by the "new" field on each option
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSortExpandedRemoteContactList_ByNew);
void exprGenSortExpandedRemoteContactList_ByNew( SA_PARAM_NN_VALID UIGen *pGen)
{
	ParseTable *pTable;
	RemoteContactDisplayRow ***peaList = (RemoteContactDisplayRow ***)ui_GenGetList(pGen, NULL, &pTable);
	if(peaList && (*peaList) && pTable == parse_RemoteContactDisplayRow)
	{
		eaQSort((*peaList), remoteContactDisplayRow_Compare_New);
	}
}

// Sorts the gen's expanded remote contact list by the contact name field on each option
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSortExpandedRemoteContactList_ByContactName);
void exprGenSortExpandedRemoteContactList_ByContactName( SA_PARAM_NN_VALID UIGen *pGen)
{
	ParseTable *pTable;
	RemoteContactDisplayRow ***peaList = (RemoteContactDisplayRow ***)ui_GenGetList(pGen, NULL, &pTable);
	if(peaList && (*peaList) && pTable == parse_RemoteContactDisplayRow)
	{
		eaQSort((*peaList), remoteContactDisplayRow_Compare_ContactName);
	}
}

// Sorts the gen's expanded remote contact list by the contact indicator field on each option
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSortExpandedRemoteContactList_ByOptionIndicator);
void exprGenSortExpandedRemoteContactList_ByOptionIndicator( SA_PARAM_NN_VALID UIGen *pGen)
{
	ParseTable *pTable;
	RemoteContactDisplayRow ***peaList = (RemoteContactDisplayRow ***)ui_GenGetList(pGen, NULL, &pTable);
	if(peaList && (*peaList) && pTable == parse_RemoteContactDisplayRow)
	{
		eaQSort((*peaList), remoteContactDisplayRow_Compare_OptionIndicator);
	}
}

static void exprGenExpandedRemoteContactList_FilterOutIndicators_List(RemoteContactDisplayRow ***peaList, ContactIndicator* eaiIndicators)
{
	static char *s_estrKeys = NULL;
	int i,j;

	if(!eaiIndicators || !eaiSize(&eaiIndicators) || !peaList || !(*peaList) || !eaSize(peaList))
		return;

	for(i=eaSize(peaList)-1; i>=0; i--)
	{
		RemoteContactDisplayRow* pRow = eaGet(peaList, i);
		RemoteContactOption* pOption = pRow ? remoteContact_GetRemoteOptionFromDisplayRow(pRow) : NULL;
		if(pOption && pOption->pOption)
		{
			ContactIndicator eIndicator = pOption->pOption->eType;

			for (j = eaiSize(&eaiIndicators)-1; j >= 0; j--)
			{
				ContactIndicator eIndicatorToMatch = eaiIndicators[j];
				if(eIndicatorToMatch == eIndicator)
					break;
			}
			if(j >= 0)
			{
				eaRemove(peaList, i);
			}
		}
	}

	// Filter out headers that no longer have any sub-data
	for (i = eaSize(peaList)-1; i >= 0; i--)
	{
		RemoteContactDisplayRow* pRow = (*peaList)[i];
		if (pRow->bIsHeader && (i + 1 >= eaSize(peaList) || (*peaList)[i+1]->bIsHeader))
		{
			eaRemove(peaList, i);
		}
	}
}

// Filters out rows with the specified indicator(s) from the gen's expanded remote contact list
// Indicators should be separated by whitespace
// DEPRECATED: Use Filter <& &>
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenExpandedRemoteContactList_FilterOutIndicators);
void exprGenExpandedRemoteContactList_FilterOutIndicators_String( SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID const char* pchIndicators)
{
	static char *s_estrKeys = NULL;
	ParseTable *pTable;
	RemoteContactDisplayRow ***peaList = (RemoteContactDisplayRow ***)ui_GenGetList(pGen, NULL, &pTable);
	ContactIndicator* eaiIndicators = NULL;
	char *ppchKeys[128];
	int count;
	int i;

	//
	// FIXME(jm): This changes the list model! This needs to be removed!
	//

	if(!pchIndicators || pchIndicators[0]=='\0' || !peaList || !(*peaList) || !eaSize(peaList) || pTable != parse_RemoteContactDisplayRow)
		return;

	estrCopy2(&s_estrKeys, pchIndicators);
	count = tokenize_line(s_estrKeys, ppchKeys, NULL);

	for (i=0; i < count; i++)
	{
		ContactIndicator eIndicator = StaticDefineIntGetInt(ContactIndicatorEnum, ppchKeys[i]);
		eaiPushUnique(&eaiIndicators, eIndicator);
	}

	exprGenExpandedRemoteContactList_FilterOutIndicators_List(peaList, eaiIndicators);
	eaiDestroy(&eaiIndicators);
}

// Filters out rows without the specified indicator(s) from the gen's expanded remote contact list
// Indicators should be separated by whitespace
// DEPRECATED: Use Filter <& &>
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenExpandedRemoteContactList_FilterOutAllIndicatorsExcept);
void exprGenExpandedRemoteContactList_FilterOutAllIndicatorsExcept( SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID const char* pchIndicators)
{
	static char *s_estrKeys = NULL;
	ParseTable *pTable;
	RemoteContactDisplayRow ***peaList = (RemoteContactDisplayRow ***)ui_GenGetList(pGen, NULL, &pTable);
	ContactIndicator* eaiIndicators = NULL;
	char *ppchKeys[128];
	int count;
	int iContactEnumMin,iContactEnumMax;
	int i,j;

	//
	// FIXME(jm): This changes the list model! This needs to be removed!
	//

	if(!pchIndicators || pchIndicators[0]=='\0' || !peaList || !(*peaList) || !eaSize(peaList) || pTable != parse_RemoteContactDisplayRow)
		return;

	DefineGetMinAndMaxInt(ContactIndicatorEnum, &iContactEnumMin, &iContactEnumMax);
	devassert(iContactEnumMin>=0); 	// We really can't deal with cases where enums are less than 0

	for(i=0; i<=iContactEnumMax; i++)
	{
		eaiPush(&eaiIndicators, i);
	}

	estrCopy2(&s_estrKeys, pchIndicators);
	count = tokenize_line(s_estrKeys, ppchKeys, NULL);
	for(i=count-1; i>=0; i--)
	{
		ContactIndicator eIndicator = StaticDefineIntGetInt(ContactIndicatorEnum, ppchKeys[i]);

		for (j = eaiSize(&eaiIndicators)-1; j >= 0; j--)
		{
			ContactIndicator eIndicatorToMatch = eaiIndicators[j];
			if(eIndicatorToMatch == eIndicator)
				break;
		}
		if(j >= 0)
		{
			eaiRemove(&eaiIndicators, j);
		}
	}

	exprGenExpandedRemoteContactList_FilterOutIndicators_List(peaList, eaiIndicators);
	eaiDestroy(&eaiIndicators);
}

// DO NOT USE
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenExpandedRemoteContactList_GroupByMissionCategory);
void exprGenExpandedRemoteContactList_GroupByMissionCategory( ExprContext *pContext, SA_PARAM_NN_VALID UIGen *pGen, SA_PARAM_NN_VALID const char* pchUncategorizedMessageKey)
{
	ParseTable *pTable;
	RemoteContactDisplayRow ***peaList = (RemoteContactDisplayRow ***)ui_GenGetList(pGen, NULL, &pTable);
	const char *prevCategory = NULL;
	int numHeaders = 0;
	int i;

	//
	// FIXME(jm): This changes the list model! This needs to be removed!
	//

	if(!peaList || !(*peaList) || !eaSize(peaList) || pTable != parse_RemoteContactDisplayRow)
		return;

	// Sort the list by mission category first
	eaQSort((*peaList), remoteContactDisplayRow_Compare_MissionCategory);

	// Add headers
	for (i = 0; i < eaSize(peaList); i++)
	{
		RemoteContactDisplayRow* pRow = eaGet(peaList, i);
		const char *pchTranslatedCategoryName = pRow ? exprRemoteContactDisplayRow_GetMissionCategory(pContext, pRow) : NULL;

		if (pchTranslatedCategoryName && (!prevCategory || stricmp(prevCategory, pchTranslatedCategoryName)))
		{
			RemoteContactDisplayRow *pNewHeader = StructCreate(parse_RemoteContactDisplayRow);
			pNewHeader->pchHeaderDisplayString = StructAllocString(pchTranslatedCategoryName);
			pNewHeader->bIsHeader = true;
			eaInsert(peaList, pNewHeader, i++);
			numHeaders++;

			prevCategory = pchTranslatedCategoryName;
		}
		else if (!pchTranslatedCategoryName && (prevCategory || !numHeaders))
		{
			RemoteContactDisplayRow *pNewHeader = StructCreate(parse_RemoteContactDisplayRow);
			pNewHeader->pchHeaderDisplayString = StructAllocString(TranslateMessageKeyDefault(pchUncategorizedMessageKey, "Uncategorized"));
			pNewHeader->bIsHeader = true;
			eaInsert(peaList, pNewHeader, i++);
			numHeaders++;

			prevCategory = NULL;
		}
	}
}

// Returns the number of remote contacts a player has
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenSizeOfRemoteContactList);
int exprGenSizeOfRemoteContactList(ExprContext *pContext) {
	Entity* pPlayerEnt = entActivePlayerPtr();
	if(pPlayerEnt) {
		RemoteContact** eaRemoteContacts = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, eaRemoteContacts);
		if(eaRemoteContacts) {
			return eaSize(&eaRemoteContacts);
		} else {
			return 0;
		}
	} else if(geaCachedRemoteContacts) {
		return eaSize(&geaCachedRemoteContacts);
	}
	return 0;
}

static bool RemoteContact_SetRemoteContactDisplayRowForMission(Entity* pPlayerEnt, const char* pchMission, ContactIndicator eIndicatorToMatch, RemoteContactDisplayRow* pOutputRow)
{
	if(pchMission && (*pchMission) && pPlayerEnt)
	{
		RemoteContact** eaRemoteContacts = SAFE_MEMBER3(pPlayerEnt, pPlayer, pInteractInfo, eaRemoteContacts);
		int i, j;

		for (i=0; i < eaSize(&eaRemoteContacts); i++)
		{
			for (j=0; j < eaSize(&eaRemoteContacts[i]->eaOptions); j++)
			{
				RemoteContactOption *pRemoteOption = eaRemoteContacts[i]->eaOptions[j];
				const char* pchOptionMission = pRemoteOption->pcMissionName;
				if(pchOptionMission && stricmp_safe(pchMission, pchOptionMission) == 0 && pRemoteOption->pOption && pRemoteOption->pOption->eType == eIndicatorToMatch)
				{
					if(pOutputRow)
					{
						pOutputRow->pchContactName = allocAddString(eaRemoteContacts[i]->pchContactDef);
						pOutputRow->pchOptionKey = StructAllocString(pRemoteOption->pchKey);
					}
					return true;
				}
			}
		}
	}
	return false;
}

// Returns true if a remote contact option is avaialble to turn in the specified mission
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRemoteContact_IsRemoteTurnInAvailableForMission);
bool exprGenRemoteContact_IsRemoteTurnInAvailableForMission(ExprContext *pContext, const char* pchMission)
{
	Entity* pPlayerEnt = entActivePlayerPtr();

	return RemoteContact_SetRemoteContactDisplayRowForMission(pPlayerEnt, pchMission, ContactIndicator_MissionCompleted, NULL);
}

// Launches a remote contact to turn in the specified mission
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenRemoteContact_LaunchRemoteTurnInForMission);
void exprGenRemoteContact_LaunchRemoteTurnInForMission(ExprContext *pContext, const char* pchMission)
{
	Entity* pPlayerEnt = entActivePlayerPtr();
	RemoteContactDisplayRow* pRow = StructCreate(parse_RemoteContactDisplayRow);

	if(RemoteContact_SetRemoteContactDisplayRowForMission(pPlayerEnt, pchMission, ContactIndicator_MissionCompleted, pRow))
	{
		exprRemoteContactDisplayRow_LaunchContactFromRow(pContext, pRow);
	}

	StructDestroy(parse_RemoteContactDisplayRow, pRow);
}

/*************************************
*	END of Remote Contact Expressions
*************************************/

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetMissionTimer");
U32 exprContactDialogGetMissionTimer(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog){
		if (pDialog->uMissionExpiredTime){
			if (pDialog->uMissionExpiredTime > timeServerSecondsSince2000()){
				return pDialog->uMissionExpiredTime - timeServerSecondsSince2000();
			} else {
				return 0;
			}
		} else if (pDialog->uMissionTimeLimit){
			return pDialog->uMissionTimeLimit;
		}
	}
	return 0;
}

// Polls the server to get a count of all the nodes on the map which have an entry of the specified category
AUTO_COMMAND ACMD_NAME(CountInteractionNodesOfType) ACMD_ACCESSLEVEL(0) ACMD_HIDE;
void countInteractionNodesOfType(const char* pchCategory)
{
	siNodeCount = -2;  // Scanning
	ServerCmd_gslCountNodesOfType(pchCategory);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ScanForClickiesAvailable);
bool exprScanForClickiesAvailable(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (!SAFE_MEMBER(pEntity, pPlayer)) {
		return false;
	} else {
		RegionRules* rr = getRegionRulesFromEnt(pEntity);
		if (!rr || !rr->bAllowScanForInteractables) {
			return false;
		} else {
			U32 timeSinceLast = timeServerSecondsSince2000() - pEntity->pPlayer->iTimeLastScanForInteractables;
			return timeSinceLast >= rr->iScanForInteractablesCooldown;
		}
	}
}

// Gets the count from the last call of countInteractionNodesOfType
// -1 = Error, -2 = Still Scanning
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GetLastInteractionNodeCount);
int exprGetLastInteractionNodeCount(ExprContext *pContext)
{
	return siNodeCount;
}

// Sets the interaction node count to -2
AUTO_EXPR_FUNC(entityutil) ACMD_NAME(ClearLastInteractionNodeCount);
void exprClearLastInteractionNodeCount(ExprContext *pContext)
{
	siNodeCount = -2;
}

// Called by the server to set the interaction node count used in the previous two functions
AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclSetInteractionNodeCount(int count)
{
	siNodeCount = count;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(HasSyncDialog);
bool exprHasSyncDialog(SA_PARAM_OP_VALID Entity* pEnt)
{
	return (pEnt && team_IsMember(pEnt) && mapState_GetSyncDialogForTeam(mapStateClient_Get(),team_GetTeamID(pEnt)));
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GetSyncDialogTimer);
int exprGetSyncDialogTimer(SA_PARAM_OP_VALID Entity* pEnt)
{
	if(pEnt && team_IsMember(pEnt)) {
		SyncDialog* pSyncDialog = mapState_GetSyncDialogForTeam(mapStateClient_Get(),team_GetTeamID(pEnt));
		if(pSyncDialog) {
			return pSyncDialog->uiExpireTime - timeSecondsSince2000();
		}
	}
	return 0;
}

// Sets the UIGen's list to the entity's synced dialog player list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetSyncDialogList);
void exprGenGetSyncDialogList( SA_PARAM_NN_VALID UIGen *pGen, Entity* pEnt) {
	static SyncDialogMember** s_eaMembers = NULL;
	if(pEnt && team_IsMember(pEnt)) {
		SyncDialog* pSyncDialog = mapState_GetSyncDialogForTeam(mapStateClient_Get(),team_GetTeamID(pEnt));
		if(pSyncDialog) {
			if(!s_eaMembers)
				eaCreate(&s_eaMembers);
			else
				eaClear(&s_eaMembers);

			eaCopyStructs(&pSyncDialog->eaMembers, &s_eaMembers, parse_SyncDialogMember);
			ui_GenSetList(pGen, &s_eaMembers, parse_SyncDialogMember);
		}
	}
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME(GetSyncDialogMemberName);
const char* exprGetSyncDialogMemberName(ExprContext *pContext, SyncDialogMember* pMember)
{
	if(pMember) {
		Entity* pEnt = entFromEntityRefAnyPartition(pMember->entRef);
		if(pEnt) {
			return entGetLangName(pEnt, entGetLanguage(pEnt));
		}
	}
	return "";
}

// Adds a new contact log entry to the cached contact log
AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void addContactLogEntry(SA_PARAM_NN_VALID ContactLogEntry* pLog)
{
	Entity* pEnt = entActivePlayerPtr();
	if(seaContactLog && pEnt && suiContactLogEnt != entGetContainerID(pEnt)) {
		eaClearStruct(&seaContactLog, parse_ContactLogEntry);
		suiContactLogEnt = entGetContainerID(pEnt);
	}
	if(pLog && pLog->pchName && pLog->pchText && pLog->uiTimestamp) {
		ContactLogEntry *pCopy = StructClone(parse_ContactLogEntry, pLog);
		if(seaContactLog && eaSize(&seaContactLog) >= MAX_CONTACT_LOG_ENTRIES) {
			eaRemove(&seaContactLog, MAX_CONTACT_LOG_ENTRIES - 1);
		}
		if (pCopy->erHeadshotEntity && !pCopy->pHeadshotCostume && !IS_HANDLE_ACTIVE(pCopy->hHeadshotCostumeRef)) {
			// This assumes a lot...
			Entity *pHeadshotEnt = entFromEntityRefAnyPartition(pCopy->erHeadshotEntity);
			PlayerCostume *pCostume = pHeadshotEnt ? costumeEntity_GetEffectiveCostume(pHeadshotEnt) : NULL;
			pCopy->pHeadshotCostume = pCostume ? StructClone(parse_PlayerCostume, pCostume) : NULL;
			pCopy->erHeadshotEntity = 0;
		}
		eaInsert(&seaContactLog, pCopy, 0);
	}
}

// Sets the UIGen's list to the contact log entry list
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetContactLog);
void exprGenGetContactLog( SA_PARAM_NN_VALID UIGen *pGen) {
	if(!seaContactLog) {
		eaCreate(&seaContactLog);
	} else {
		Entity* pEnt = entActivePlayerPtr();
		if(pEnt && entGetContainerID(pEnt) != suiContactLogEnt) {
			eaClearStruct(&seaContactLog, parse_ContactLogEntry);
		}
	}

	ui_GenSetList(pGen, &seaContactLog, parse_ContactLogEntry);
}

// Formats the timestamp for the contact log entry
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetContactLogTimestamp);
const char* exprGenGetContactLogTimestamp( ExprContext *pContext, SA_PARAM_OP_VALID ContactLogEntry* pLogEntry, const char* pchTimeFormatKey ) {
	char* result = NULL;
	char* estrResult = NULL;

	if(pLogEntry && EMPTY_TO_NULL(pchTimeFormatKey)) {
		langFormatGameMessageKey(langGetCurrent(), &estrResult, pchTimeFormatKey, STRFMT_DATETIME("Timestamp", pLogEntry->uiTimestamp), STRFMT_END);

		if (estrResult)
		{
			result = exprContextAllocScratchMemory(pContext, strlen(estrResult) + 1);
			memcpy(result, estrResult, strlen(estrResult) + 1);
			estrDestroy(&estrResult);
		}
	}
	return NULL_TO_EMPTY(result);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetContactLogEntryText);
const char* exprGenGetContactLogEntryText(SA_PARAM_OP_VALID ContactLogEntry* pLogEntry) {
	if(pLogEntry && EMPTY_TO_NULL(pLogEntry->pchText)) {
		return pLogEntry->pchText;
	}

	return "";
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetContactLogEntryName);
const char* exprGenGetContactLogEntryName(SA_PARAM_OP_VALID ContactLogEntry* pLogEntry) {
	if(pLogEntry && EMPTY_TO_NULL(pLogEntry->pchName)) {
		return pLogEntry->pchName;
	}

	return "";
}

// Returns the time remaining on the specified contact option's cooldown timer
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenContactOptionCooldownTimeLeft);
int exprGenContactOptionCooldownTimeLeft(SA_PARAM_OP_VALID ContactDialogOption* pOption) {
	if(pOption)
	{
		return pOption->uCooldownExpireTime - timeServerSecondsSince2000();
	}

	return 0;
}

// Formats the time remaining on the specified contact option's cooldown timer to a string based on the passed in message
// Uses STRFMT_TIME to format the time.
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenContactOptionCooldownTimeLeftString);
const char* exprGenContactOptionCooldownTimeLeftString( ExprContext* pContext, SA_PARAM_OP_VALID ContactDialogOption* pOption, ACMD_EXPR_DICT(Message) const char* pchMessageKey) {
	char* estrTimeLeft = NULL;
	char* result = NULL;

	if(pchMessageKey && pchMessageKey[0])
	{
		int iTimeLeft = 0;
		if(pOption)
		{
			iTimeLeft = pOption->uCooldownExpireTime - timeServerSecondsSince2000();
		}

		FormatGameMessageKey(&estrTimeLeft, pchMessageKey, STRFMT_TIMER("Time", iTimeLeft), STRFMT_END);

		if (estrTimeLeft)
		{
			result = exprContextAllocScratchMemory(pContext, strlen(estrTimeLeft) + 1);
			memcpy(result, estrTimeLeft, strlen(estrTimeLeft) + 1);
			estrDestroy(&estrTimeLeft);
		}
	}

	return NULL_TO_EMPTY(result);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetStoreRegion);
const char* exprGenGetStoreRegion(ExprContext* pContext, SA_PARAM_OP_VALID Entity* pEntity)
{
	CONTACT_DIALOG_ENUM_STR_GETTER(pEntity, WorldRegionType, eStoreRegion, "None");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(GenGetContactDialogOptionType);
const char* exprGetContactDialogOptionType(ExprContext* pContext, SA_PARAM_OP_VALID ContactDialogOption* pOption)
{
	if(pOption)
	{
		return StaticDefineIntRevLookup(ContactIndicatorEnum, pOption->eType);
	}

	return "NoInfo";
}

// Returns the text to be displayed as the "buy" tab of the current contact
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactGetBuyOptionText");
const char *exprContactGetBuyOptionText(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, pchBuyOptionText, NULL);
}

// Returns the text to be displayed as the "buy" tab of the current contact
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactGetSellOptionText");
const char *exprContactGetSellOptionText(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, pchSellOptionText, NULL);
}

// Returns the text to be displayed as the "buy" tab of the current contact
AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactGetBuyBackOptionText");
const char *exprContactGetBuyBackOptionText(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, pchBuyBackOptionText, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactTeamDialogIsAnyChoiceMade);
bool contactUI_TeamDialogIsAnyChoiceMade(void)
{
	return s_pchLastDialogChoiceKey != NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactTeamDialogSetChoice);
void contactUI_TeamDialogSetChoice(const char *pchDialogKey)
{
	if (pchDialogKey)
	{
		s_pchLastDialogChoiceKey = strdup(pchDialogKey);
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactTeamDialogClearChoice);
void contactUI_TeamDialogClearChoiceInternal(void)
{
	if (s_pchLastDialogChoiceKey)
	{
		free(s_pchLastDialogChoiceKey);
		s_pchLastDialogChoiceKey = NULL;
	}
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactTeamDialogClearVotes);
void contactUI_TeamDialogClearVotes(void)
{
	// Deprecated
}

// Called whenever the team spokesman makes a dialog choice
AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void contactUI_TeamDialogChoiceMade(const char *pchDialogKey, bool bSpokesmanWillChange)
{
	contactUI_TeamDialogClearChoiceInternal();

	s_bSpokesmanWillChange = bSpokesmanWillChange;

	contactUI_TeamDialogSetChoice(pchDialogKey);
}



AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void contactUI_TeamDialogClearChoice(void)
{
	// Clear the team spokesman choice
	contactUI_TeamDialogClearChoiceInternal();

	// Clear all votes
	contactUI_TeamDialogClearVotes();
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactTeamDialogHasVoted);
bool contactUI_TeamDialogHasVoted(void)
{
	Entity *pEnt = entActivePlayerPtr();
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	return SAFE_MEMBER(pDialog, bHasVoted);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactTeamDialogGetVoteTimeleft);
bool contactUI_TeamDialogGetVoteTimeleft(void)
{
	Entity *pEnt = entActivePlayerPtr();
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog && pDialog->uTeamDialogStartTime)
	{
		U32 uCurrentTime = timeServerSecondsSince2000();
		U32 uTimeoutTime = pDialog->uTeamDialogStartTime + g_ContactConfig.uTeamDialogResponseTimeout;

		if (uTimeoutTime > uCurrentTime)
		{
			return uTimeoutTime - uCurrentTime;
		}
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactTeamDialogVote);
void contactUI_TeamDialogVote(const char *pchDialogKey)
{
	Entity *pEnt = entActivePlayerPtr();
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	if (pEnt &&
		pchDialogKey && *pchDialogKey &&
		pDialog &&
		pDialog->bViewOnlyDialog &&
		(!pDialog->bHasVoted || g_ContactConfig.bTeamDialogAllowRevote))
	{
		// Let all other team members know about this vote
		ServerCmd_ContactTeamDialogVote(pchDialogKey);
	}
}

static bool contactUI_TeamMemberEligibleToVote(SA_PARAM_NN_VALID Entity *pEnt, U32 iEntID, SA_PARAM_NN_STR const char *pchDialogKey)
{
	S32 iPartitionIdx = entGetPartitionIdx(pEnt);

	if (iEntID)
	{
		ContactDialogOption *pVotedDialogOption = contactUI_GetDialogOptionByKey(pEnt, pchDialogKey);

		if (pVotedDialogOption)
		{
			// See if the team member is actually eligible to select this option
			S32 i;
			for (i = 0; i < ea32Size(&pVotedDialogOption->piTeamMembersEligibleToInteract); i++)
			{
				if (pVotedDialogOption->piTeamMembersEligibleToInteract[i] == iEntID)
				{
					return true;
				}
			}
		}
	}
	return false;
}

// Returns the voted dialog key for the entity
static const char * contactUI_TeamDialogGetVoteByEntity(Entity *pEnt)
{
	ContactDialog* pDialog = SAFE_MEMBER3(pEnt, pPlayer, pInteractInfo, pContactDialog);

	if (pDialog)
	{
		FOR_EACH_IN_EARRAY_FORWARDS(pDialog->eaTeamDialogVotes, TeamDialogVote, pVote)
		{
			if (pVote && pVote->iEntID == pEnt->myContainerID)
				return pVote->pchDialogKey;
		}
		FOR_EACH_END
	}
	return NULL;
}


// Used in sorting team dialog participants
static int contactUI_TeamDialogParticipantDataComparator(const TeamDialogParticipantData ** ppData1, const TeamDialogParticipantData ** ppData2)
{
	const TeamDialogParticipantData *pParticipant1 = ppData1 ? *ppData1 : NULL;
	const TeamDialogParticipantData *pParticipant2 = ppData2 ? *ppData2 : NULL;

	if (pParticipant1 == NULL && pParticipant2 == NULL)
		return 0;

	if (pParticipant1 == NULL)
		return -1;

	if (pParticipant2 == NULL)
		return 1;

	if (pParticipant1->bIsTeamSpokesman && !pParticipant2->bIsTeamSpokesman)
		return -1;

	if (!pParticipant1->bIsTeamSpokesman && pParticipant2->bIsTeamSpokesman)
		return 1;

	return stricmp(pParticipant1->pchName, pParticipant2->pchName);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactTeamDialogGetParticipants);
S32 contactUI_TeamDialogGetParticipants(SA_PARAM_OP_VALID UIGen *pGen)
{
	if (pGen)
	{
		// Get the list stored in the gen
		TeamDialogParticipantData *** peaParticipantData = ui_GenGetManagedListSafe(pGen, TeamDialogParticipantData);
		TeamDialogParticipantData *pParticipantData;

		// Get the active player entity
		Entity *pEnt = entActivePlayerPtr();

		Team *pTeam = team_GetTeam(pEnt);

		if (pEnt && pTeam && pEnt->pTeam->bInTeamDialog)
		{
			S32 iParticipantIndex = 0;
			FOR_EACH_IN_EARRAY_FORWARDS(pTeam->eaMembers, TeamMember, pTeamMember)
			{
				Entity *pEntTeamMember = pTeamMember ? entFromContainerIDAnyPartition(GLOBALTYPE_ENTITYPLAYER, pTeamMember->iEntID) : NULL;

				// Is this team member in a team dialog?
				if (pEntTeamMember)
				{
					const char *pchDialogKey = contactUI_TeamDialogGetVoteByEntity(pEntTeamMember);

					pParticipantData = eaGetStruct(peaParticipantData, parse_TeamDialogParticipantData, iParticipantIndex);
					pParticipantData->pEnt = pEntTeamMember;
					pParticipantData->bIsTeamSpokesman = pEntTeamMember->pTeam->bIsTeamSpokesman;
					if (pParticipantData->pchName)
						StructFreeString(pParticipantData->pchName);
					pParticipantData->pchName = StructAllocString(entGetLocalName(pEntTeamMember));
					if (pParticipantData->pchVotedDialogKey)
						StructFreeString(pParticipantData->pchVotedDialogKey);
					pParticipantData->pchVotedDialogKey = StructAllocString(pchDialogKey);

					++iParticipantIndex;
				}
			}
			FOR_EACH_END

			// Clean up unused data
			while (eaSize(peaParticipantData) > iParticipantIndex)
				StructDestroy(parse_TeamDialogParticipantData, eaPop(peaParticipantData));

			// Sort the array so the team spokesman is always at the top
			eaQSort(*peaParticipantData, contactUI_TeamDialogParticipantDataComparator);

			ui_GenSetManagedListSafe(pGen, peaParticipantData, TeamDialogParticipantData, true);

			return iParticipantIndex;
		}
		else
		{
			ui_GenSetManagedListSafe(pGen, NULL, TeamDialogParticipantData, true);
			return 0;
		}
	}

	return 0;
}

// Get the number of dialog texts there are
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogTextCount);
S32 exprContactDialogTextCount(SA_PARAM_OP_VALID Entity *pEnt)
{
	InteractInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	if (pInfo && pInfo->pContactDialog)
	{
		return pInfo->pContactDialog->pchDialogText2 && *pInfo->pContactDialog->pchDialogText2 ? 2 : 1;
	}
	return 0;
}

// Get the header for the given dialog index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogHeader);
const char *exprContactGetDialogHeader(SA_PARAM_OP_VALID Entity *pEnt, S32 iIndex)
{
	InteractInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	if (pInfo && pInfo->pContactDialog)
	{
		switch (iIndex)
		{
		case 0:
			return pInfo->pContactDialog->pchDialogHeader;
		case 1:
			return pInfo->pContactDialog->pchDialogHeader2;
		}
	}
	return "";
}

// Get the text for the given dialog index
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogText);
const char *exprContactGetDialogText(SA_PARAM_OP_VALID Entity *pEnt, S32 iIndex)
{
	InteractInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);
	if (pInfo && pInfo->pContactDialog)
	{
		switch (iIndex)
		{
		case 0:
			return pInfo->pContactDialog->pchDialogText1;
		case 1:
			return pInfo->pContactDialog->pchDialogText2;
		}
	}
	return "";
}

// Returns the formatted dialog text for the current contact dialog (This function only supports the pchDialogText1 field)
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactGetFormattedDialogText);
ExprFuncReturnVal exprContactGetFormattedDialogText(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, SA_PARAM_OP_VALID Entity *pEnt, ACMD_EXPR_ERRSTRING errEstr)
{
	static char *s_pchOut;
	InteractInfo *pInfo = SAFE_MEMBER2(pEnt, pPlayer, pInteractInfo);

	estrClear(&s_pchOut);

	if (pInfo &&
		pInfo->pContactDialog &&
		pInfo->pContactDialog->pchDialogText1 &&
		pInfo->pContactDialog->pchDialogText1[0])
	{
		ContactDialogFormatterDef *pFormatter = GET_REF(pInfo->pContactDialog->hDialogText1Formatter);

		if (pFormatter && GET_REF(pFormatter->msgDialogFormat.hMessage))
		{
			FormatDisplayMessage(&s_pchOut,
				pFormatter->msgDialogFormat,
				STRFMT_STRING("DialogText", pInfo->pContactDialog->pchDialogText1),
				STRFMT_END);
		}
		else
		{
			// Use the text directly as there is no formatter selected
			estrAppend2(&s_pchOut, pInfo->pContactDialog->pchDialogText1);
		}
	}
	else
	{
		estrAppend2(&s_pchOut, "");
	}

	*ppchOut = exprContextAllocString(pContext, s_pchOut);
	return ExprFuncReturnFinished;
}

// Returns the formatted text for the given dialog option
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactGetFormattedDialogOptionText);
ExprFuncReturnVal exprContactGetFormattedDialogOptionText(ExprContext *pContext, ACMD_EXPR_STRING_OUT ppchOut, SA_PARAM_OP_VALID ContactDialogOption *pOption, ACMD_EXPR_ERRSTRING errEstr)
{
	static char *s_pchOut;

	estrClear(&s_pchOut);

	if (pOption && pOption->pchDisplayString && pOption->pchDisplayString[0])
	{
		ContactDialogFormatterDef *pFormatter = GET_REF(pOption->hDialogFormatter);

		if (pFormatter && GET_REF(pFormatter->msgDialogFormat.hMessage))
		{
			FormatDisplayMessage(&s_pchOut,
				pFormatter->msgDialogFormat,
				STRFMT_STRING("DialogText", pOption->pchDisplayString),
				STRFMT_END);
		}
		else
		{
			// Use the text directly as there is no formatter selected
			estrAppend2(&s_pchOut, pOption->pchDisplayString);
		}
	}
	else
	{
		estrAppend2(&s_pchOut, "");
	}

	*ppchOut = exprContextAllocString(pContext, s_pchOut);
	return ExprFuncReturnFinished;
}

// Get the team size
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogTeamSize);
S32 exprContactDialogTeamSize(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, iTeamSize, 0);
}

// Check to see if it scales for the team size
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogScalesForTeam);
bool exprContactDialogScalesForTeam(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, bScalesForTeam, false);
}

// Get the time limit for the mission
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogMissionTimeLimit);
S32 exprContactDialogMissionTimeLimit(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, uMissionTimeLimit, 0);
}

// Get the lockout type
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogMissionLockoutType);
S32 exprContactDialogMissionLockoutType(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, eMissionLockoutType, MissionLockoutType_None);
}

// Get the credit type
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogMissionCreditType);
S32 exprContactDialogMissionCreditType(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, eMissionCreditType, MissionCreditType_Primary);
}

// Get the rewards header
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogRewardsHeader);
const char *exprContactDialogRewardsHeader(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, pchRewardsHeader, NULL);
}

// Get the rewards header
AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogOptionalRewardsHeader);
const char *exprContactDialogOptionalRewardsHeader(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, pchOptionalRewardsHeader, NULL);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogHasProvisioning);
bool exprContactDialogHasProvisioning(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	ContactDialog *pDialog = SAFE_MEMBER3(pPlayer, pPlayer, pInteractInfo, pContactDialog);
	return pDialog && eaSize(&pDialog->eaProvisioning) > 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetProvisioningCount);
S32 exprContactDialogGetProvisioningCount(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	ContactDialog *pDialog = SAFE_MEMBER3(pPlayer, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog && eaSize(&pDialog->eaProvisioning) > 0)
	{
		return pDialog->eaProvisioning[0]->iNumericValue;
	}
	return 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetProvisioningName);
const char *exprContactDialogGetProvisioningName(void)
{
	Entity *pPlayer = entActivePlayerPtr();
	ContactDialog *pDialog = SAFE_MEMBER3(pPlayer, pPlayer, pInteractInfo, pContactDialog);
	if (pDialog && eaSize(&pDialog->eaProvisioning) > 0)
	{
		return pDialog->eaProvisioning[0]->estrNumericName;
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetProvisioningList);
void exprContactDialogGetProvisioningList(SA_PARAM_NN_VALID UIGen *pGen)
{
	static ContactDialogStoreProvisioning **s_None;
	Entity *pPlayer = entActivePlayerPtr();
	ContactDialog *pDialog = SAFE_MEMBER3(pPlayer, pPlayer, pInteractInfo, pContactDialog);
	ui_GenSetListSafe(pGen, pDialog ? &pDialog->eaProvisioning : &s_None, ContactDialogStoreProvisioning);
}

// The version number stored for the contact dialog
static U32 s_uiStoredContactDialogVersion = 0;
static U32 s_uiLastTeamSpokesmanID = 0;

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogStoreVersion);
void exprContactDialogStoreVersion(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	if (pContactDialog)
	{
		s_uiStoredContactDialogVersion = pContactDialog->uiVersion;
		s_uiLastTeamSpokesmanID = pContactDialog->iTeamSpokesmanID;
	}
	s_bSpokesmanWillChange = false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogResetVersion);
void exprContactDialogResetVersion(SA_PARAM_OP_VALID Entity *pEntity)
{
	s_uiStoredContactDialogVersion = 0;
	s_uiLastTeamSpokesmanID = 0;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogVersionChanged);
bool exprContactDialogVersionChanged(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	if (pContactDialog)
	{
		return pContactDialog->uiVersion != s_uiStoredContactDialogVersion || pContactDialog->iTeamSpokesmanID != s_uiLastTeamSpokesmanID;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogTeamSpokesmanWillChange);
bool exprContactDialogTeamSpokesmanWillChange(SA_PARAM_OP_VALID Entity *pEntity)
{
	ContactDialog *pContactDialog = SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog);

	if (pContactDialog)
	{
		return s_bSpokesmanWillChange;
	}

	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogMinigameType);
S32 exprContactDialogMinigameType(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, eMinigameType, kMinigameType_None);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogCanChooseOption);
bool exprContactDialogCanChooseOption(SA_PARAM_OP_VALID Entity *pEntity, SA_PARAM_OP_VALID ContactDialogOption *pDialogOption)
{
	return contactUI_CanChooseDialogOption(pEntity, pDialogOption, true);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("ContactDialogGetIndicator");
const char * exprContactDialogGetIndicator(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_ENUM_STR_GETTER(pEntity, ContactIndicator, eIndicator, "");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("ContactDialogGetLastResponseText");
const char * exprContactDialogGetLastResponseText(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, pchLastResponseDisplayString, "");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogOfficerTrainer);
bool exprContactDialogOfficerTrainer(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, bIsOfficerTrainer, false);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogSkillType);
S32 exprContactDialogSkillType(SA_PARAM_OP_VALID Entity *pEntity)
{
	CONTACT_DIALOG_SIMPLE_GETTER(pEntity, iSkillType, kSkillType_None);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("RemoteContactFindMissionContact");
const char *exprRemoteContactFindMissionContact(const char *pchMissionDef)
{
	Entity *pEntity = entActivePlayerPtr();
	InteractInfo *pInfo = SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo);
	S32 i, j;
	pchMissionDef = allocFindString(pchMissionDef);
	if (pchMissionDef)
	{
		for (i = eaSize(&pInfo->eaRemoteContacts) - 1; i >= 0; --i)
		{
			RemoteContact *pRemoteContact = pInfo->eaRemoteContacts[i];
			for (j = eaSize(&pRemoteContact->eaOptions) - 1; j >= 0; --j)
			{
				RemoteContactOption *pRemoteOption = pRemoteContact->eaOptions[j];
				if (pRemoteOption->pcMissionName == pchMissionDef)
				{
					return pRemoteContact->pchContactDef;
				}
			}
		}
	}
	return NULL;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME("RemoteContactStartForMission");
bool exprRemoteContactStartForMission(const char *pchMissionDef)
{
	Entity *pEntity = entActivePlayerPtr();
	InteractInfo *pInfo = SAFE_MEMBER2(pEntity, pPlayer, pInteractInfo);
	S32 i, j;
	pchMissionDef = allocFindString(pchMissionDef);
	if (pchMissionDef)
	{
		for (i = eaSize(&pInfo->eaRemoteContacts) - 1; i >= 0; --i)
		{
			RemoteContact *pRemoteContact = pInfo->eaRemoteContacts[i];
			for (j = eaSize(&pRemoteContact->eaOptions) - 1; j >= 0; --j)
			{
				RemoteContactOption *pRemoteOption = pRemoteContact->eaOptions[j];
				if (pRemoteOption->pcMissionName == pchMissionDef)
				{
					ServerCmd_contact_StartRemoteContactWithOption(pRemoteContact->pchContactDef, pRemoteOption->pchKey);
					return true;
				}
			}
		}
	}
	return false;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetContactType);
S32 exprContactDialogGetContactType(SA_PARAM_OP_VALID Entity *pEntity)
{
	if (SAFE_MEMBER3(pEntity, pPlayer, pInteractInfo, pContactDialog))
		return pEntity->pPlayer->pInteractInfo->pContactDialog->eContactType;
	else
		return -1;
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactTrainerIsNearby);
bool contactUI_TrainerIsNearby(void)
{
	Entity *pEnt = entActivePlayerPtr();
	if(pEnt && pEnt->pPlayer && (pEnt->pPlayer->InteractStatus.eNearbyContactTypes & ContactFlag_PowersTrainer))
	{
		return true;
	}

	return false;
}

// Called by the server to show the RemoteContacts_Window Gen
AUTO_COMMAND ACMD_CLIENTCMD ACMD_ACCESSLEVEL(0) ACMD_PRIVATE;
void gclShowRemoteContacts()
{
	UIGen *pGen = ui_GenFind(REMOTE_CONTACTS_WINDOW_GEN, kUIGenTypeNone);
	if(pGen)
		ui_GenSendMessage(pGen, "Show");
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetHeader);
const char * exprContactDialogGetHeader(void)
{
	Entity *pEntity = entActivePlayerPtr();

	return SAFE_MEMBER4(pEntity, pPlayer, pInteractInfo, pContactDialog, pchDialogHeader);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetText1);
const char * exprContactDialogGetText1(void)
{
	Entity *pEntity = entActivePlayerPtr();

	return SAFE_MEMBER4(pEntity, pPlayer, pInteractInfo, pContactDialog, pchDialogText1);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetText2);
const char * exprContactDialogGetText2(void)
{
	Entity *pEntity = entActivePlayerPtr();

	return SAFE_MEMBER4(pEntity, pPlayer, pInteractInfo, pContactDialog, pchDialogText2);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetOptionalRewardsHeader);
const char * exprContactDialogGetOptionalRewardsHeader(void)
{
	Entity *pEntity = entActivePlayerPtr();

	return SAFE_MEMBER4(pEntity, pPlayer, pInteractInfo, pContactDialog, pchOptionalRewardsHeader);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetListHeader);
const char * exprContactDialogGetListHeader(void)
{
	Entity *pEntity = entActivePlayerPtr();

	return SAFE_MEMBER4(pEntity, pPlayer, pInteractInfo, pContactDialog, pchListHeader);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetRewardsHeader);
const char * exprContactDialogGetRewardsHeader(void)
{
	Entity *pEntity = entActivePlayerPtr();

	return SAFE_MEMBER4(pEntity, pPlayer, pInteractInfo, pContactDialog, pchRewardsHeader);
}

AUTO_EXPR_FUNC(UIGen) ACMD_NAME(ContactDialogGetMissionUIType);
S32 exprContactDialogGetMissionUIType(void)
{
	Entity *pEntity = entActivePlayerPtr();

	return SAFE_MEMBER4(pEntity, pPlayer, pInteractInfo, pContactDialog, eMissionUIType);
}

#include "contactui_eval_h_ast.c"
#include "contactui_eval_c_ast.c"
