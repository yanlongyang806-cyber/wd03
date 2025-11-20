#include "gclCivilian.h"
#include "aiStructCommon.h"
#include "Entity.h"
#include "entCritter.h"
#include "MemoryPool.h"
#include "dynSkeleton.h"
#include "dynAnimInterface.h"
#include "wlTime.h"
#include "../StaticWorld/ZoneMap.h"
#include "gclCivilian_c_ast.h"
#include "File.h"

#include "AutoGen/AILib_autogen_ServerCmdWrappers.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););


MP_DEFINE(AIClientCivilian);

static F32 s_fBrakeLingerTime = 2.0f;
static F32 s_fTurnLingerTime = 5.f;
static F32 s_fTurnCheckingTime = 2.f;

static F32 s_fBrakingSpeedDelta = 1.5f;
static F32 s_fTurningThreshold = RAD(45.f);
static F32 s_fTurningSuspectThreshold = RAD(5.f);

typedef struct AIClientCivilian
{
	Vec3	vLastDir;
	Vec3	vLastTurnDir;
	F32		fLastSpeed;

	S64		turningTimer;
	S64		brakeTimer;

	U32		bHeadlightsOn : 1;
	U32		bTurningLeft : 1;
	U32		bTurningRight : 1;
	U32		bBraking : 1;
	U32		bCheckTurningVec : 1;
	U32		bInitSkeleton : 1;

} AIClientCivilian;

AUTO_STRUCT;
typedef struct AIClientCivilianMap
{
	const char *pchMapName;					AST(NAME("MapName") POOL_STRING)

	F32 dayTimeMin;
	F32 dayTimeMax;
	
} AIClientCivilianMap;


AUTO_STRUCT;
typedef struct AIClientCivilianDef
{
	const char *pchHeadlightTagName;			AST(NAME("HeadlightTagName") POOL_STRING)
	const char *pchBrakeLightsTagName;			AST(NAME("BrakeLightsTagName") POOL_STRING)
	const char *pchLeftTurningTagName;			AST(NAME("LeftTurningTagName") POOL_STRING)
	const char *pchRightTurningTagName;			AST(NAME("RightTurningTagName") POOL_STRING)

	AIClientCivilianMap **eaClientCivMapInfos;	AST(NAME("CivMapInfo"))

	AST_STOP
	// current map info
	AIClientCivilianMap *pCurMapInfo;

} AIClientCivilianDef;

static AIClientCivilianDef s_clientCivDef = {0};


// ----------------------------------------------------
static struct {

	AICivClientMapDefInfo	civLegDefInfo;
	bool					civLegDefInfoDirty;
	bool					civLegDefSentReq;

	S32					curErrorIdx;
	AICivRegenReport	regenReport;
	F32					fAreaRegen;
} s_civEditorInfo = {0};


AUTO_STARTUP(ClientCivilian);
void gclCivilian_Startup(void)
{
	StructInit(parse_AIClientCivilianDef, &s_clientCivDef);

	ParserLoadFiles(NULL,"defs/config/clientCivilian.def","clientCivilian.bin",PARSER_OPTIONALFLAG,parse_AIClientCivilianDef,&s_clientCivDef);
	
	s_civEditorInfo.civLegDefInfoDirty = true;
	s_civEditorInfo.civLegDefSentReq = false;
	s_civEditorInfo.fAreaRegen = 500.f;
}

// --------------------------------------------------------------------------------------
bool gclCivilian_IsCivilian(Entity *e)
{
	return (e->pCritter && e->pCritter->eCritterSubType == CritterSubType_CIVILIAN_CAR);

}

// --------------------------------------------------------------------------------------
static void gclCivilian_TurnOnHeadLights(Entity *e, AIClientCivilian *civInfo, bool bTurnOn)
{
	if (!s_clientCivDef.pchHeadlightTagName)
		return;

	bTurnOn = !!bTurnOn;
	if (bTurnOn != (bool)civInfo->bHeadlightsOn)
	{
		DynSkeleton *skel = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		if (skel)
		{
			dynSkeletonShowModelsAttachedToBone(skel, s_clientCivDef.pchHeadlightTagName, bTurnOn);
			civInfo->bHeadlightsOn = bTurnOn;
		}
	}
}

// --------------------------------------------------------------------------------------
static void gclCivilian_TurnOnBrakelights(Entity *e, AIClientCivilian *civInfo, bool bTurnOn)
{
	if (!s_clientCivDef.pchBrakeLightsTagName)
		return;

	bTurnOn = !!bTurnOn;
	if (bTurnOn != (bool)civInfo->bBraking)
	{
		DynSkeleton *skel = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		if (skel)
		{
			dynSkeletonShowModelsAttachedToBone(skel, s_clientCivDef.pchBrakeLightsTagName, bTurnOn);
			civInfo->bBraking = bTurnOn;
		}
	}

	if (bTurnOn)
	{
		civInfo->brakeTimer = ABS_TIME;
	}
}

// --------------------------------------------------------------------------------------
static void gclCivilian_TurnOnLeftTurnSignal(Entity *e, AIClientCivilian *civInfo, bool bTurnOn)
{
	if (!s_clientCivDef.pchLeftTurningTagName)
		return;

	bTurnOn = !!bTurnOn;
	if (bTurnOn != (bool)civInfo->bTurningLeft)
	{
		DynSkeleton *skel = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		if (skel)
		{
			dynSkeletonShowModelsAttachedToBone(skel, s_clientCivDef.pchLeftTurningTagName, bTurnOn);
			civInfo->bTurningLeft = bTurnOn;
		}
	}

	if (bTurnOn)
	{
		civInfo->turningTimer = ABS_TIME;
	}
}

// --------------------------------------------------------------------------------------
static void gclCivilian_TurnOnRightTurnSignal(Entity *e, AIClientCivilian *civInfo, bool bTurnOn)
{
	if (!s_clientCivDef.pchRightTurningTagName)
		return;
	
	bTurnOn = !!bTurnOn;
	if (bTurnOn != (bool)civInfo->bTurningRight)
	{
		DynSkeleton *skel = dynSkeletonFromGuid(e->dyn.guidSkeleton);
		if (skel)
		{
			dynSkeletonShowModelsAttachedToBone(skel, s_clientCivDef.pchRightTurningTagName, bTurnOn);
			civInfo->bTurningRight = bTurnOn;
		}
	}

	if (bTurnOn)
	{
		civInfo->turningTimer = ABS_TIME;
	}
}

// --------------------------------------------------------------------------------------
// meant
static void gclCivilian_InitCostume(Entity *e, AIClientCivilian *civInfo, bool bTurnOnHeadlights)
{
	DynSkeleton *skel = dynSkeletonFromGuid(e->dyn.guidSkeleton);
	if (skel)
	{
		if (s_clientCivDef.pchHeadlightTagName && !bTurnOnHeadlights)
			dynSkeletonShowModelsAttachedToBone(skel, s_clientCivDef.pchHeadlightTagName, false);
		if (s_clientCivDef.pchBrakeLightsTagName)
			dynSkeletonShowModelsAttachedToBone(skel, s_clientCivDef.pchBrakeLightsTagName, false);
		if (s_clientCivDef.pchRightTurningTagName)
			dynSkeletonShowModelsAttachedToBone(skel, s_clientCivDef.pchRightTurningTagName, false);
		if (s_clientCivDef.pchLeftTurningTagName)
			dynSkeletonShowModelsAttachedToBone(skel, s_clientCivDef.pchLeftTurningTagName, false);

		civInfo->bHeadlightsOn = !!bTurnOnHeadlights;
		civInfo->bBraking = false;
		civInfo->bTurningLeft = false;
		civInfo->bTurningRight = false;
		civInfo->bInitSkeleton = true;
	}
	else
	{
		civInfo->bHeadlightsOn = true;
		civInfo->bBraking = true;
		civInfo->bTurningLeft = true;
		civInfo->bTurningRight = true;
		civInfo->bInitSkeleton = false;
	}

	
}

#define ASSUME_ENDOF_DAY_TIME	24

// --------------------------------------------------------------------------------------
static void gclCivilian_FindCurrentMap()
{
	const char *pcMapName = zmapGetName(NULL);
	if (pcMapName)
	{
		FOR_EACH_IN_EARRAY(s_clientCivDef.eaClientCivMapInfos, AIClientCivilianMap, pCivDefMap)
		{
			if (pCivDefMap->pchMapName == pcMapName)
			{
				s_clientCivDef.pCurMapInfo = pCivDefMap;
				return;
			}
		}
		FOR_EACH_END
	}

	s_clientCivDef.pCurMapInfo = NULL;
}

// --------------------------------------------------------------------------------------
static bool gclCivilian_IsNightTime()
{
	if (s_clientCivDef.pCurMapInfo && 
		s_clientCivDef.pCurMapInfo->dayTimeMin < s_clientCivDef.pCurMapInfo->dayTimeMax)
	{
		AIClientCivilianMap *pCivMapDef = s_clientCivDef.pCurMapInfo;
		F32 fCurTime = wlTimeGetClientTime();
		if (pCivMapDef->dayTimeMin >= fCurTime && fCurTime < pCivMapDef->dayTimeMax)
		{
			return true;
		}

		if (pCivMapDef->dayTimeMax > ASSUME_ENDOF_DAY_TIME)
		{
			F32 fWrappedTimeMax = pCivMapDef->dayTimeMax - ASSUME_ENDOF_DAY_TIME;
			if (fCurTime < fWrappedTimeMax)
				return true;
		}
	}

	return true;
}

// --------------------------------------------------------------------------------------
void gclCivilian_Initialize(Entity *e)
{
	if (!e || !e->pCritter)
		return;

	if (e->pCritter->eCritterSubType == CritterSubType_CIVILIAN_CAR)
	{
		// 
		gclCivilian_FindCurrentMap();

		if (!e->pCritter->clientCivInfo)
		{
			AIClientCivilian *pClientCiv;
			MP_CREATE(AIClientCivilian, 20);

			pClientCiv = MP_ALLOC(AIClientCivilian);
			if (pClientCiv)
			{
				ZeroStruct(pClientCiv);
				entCopyVelocityFG(e, pClientCiv->vLastDir);
				pClientCiv->fLastSpeed = normalVec3(pClientCiv->vLastDir);
				copyVec3(pClientCiv->vLastDir, pClientCiv->vLastDir);
				gclCivilian_InitCostume(e, pClientCiv, !gclCivilian_IsNightTime());

				e->pCritter->clientCivInfo = pClientCiv;
			}
		}
	}
}

// --------------------------------------------------------------------------------------
void gclCivilian_CleanUp(Entity *e)
{
	if (!e || !e->pCritter)
		return;

	if (e->pCritter->clientCivInfo)
	{
		MP_FREE(AIClientCivilian, e->pCritter->clientCivInfo);
		e->pCritter->clientCivInfo = NULL;
	}
}



// --------------------------------------------------------------------------------------
void gclCivilian_Tick(Entity *e)
{
	AIClientCivilian *civInfo;
	Vec3 vCurrentDir;
	F32 fCurrentSpeed;
	if (!e || !e->pCritter || !e->pCritter->clientCivInfo)
		return;

	civInfo = e->pCritter->clientCivInfo;
	
	if(!civInfo->bInitSkeleton)
	{
		gclCivilian_InitCostume(e, civInfo, gclCivilian_IsNightTime());
		return;
	}
	
	entCopyVelocityFG(e, vCurrentDir);
	fCurrentSpeed = normalVec3(vCurrentDir);

	// check if it is nighttime
	gclCivilian_TurnOnHeadLights(e, civInfo, gclCivilian_IsNightTime());

	// check breaking, if we're stopped or slowing beyond a certain threshold
	if (fCurrentSpeed == 0.f || 
		(civInfo->fLastSpeed - fCurrentSpeed) > s_fBrakingSpeedDelta)
	{
		gclCivilian_TurnOnBrakelights(e, civInfo, true);
	}
	else if (civInfo->bBraking)
	{	// after we've started breaking, let the brake lights stay on for a little bit before we turn them off
		if (ABS_TIME_PASSED(civInfo->brakeTimer, s_fBrakeLingerTime))
		{
			gclCivilian_TurnOnBrakelights(e, civInfo, false);
		}
	}

	// turning checks
	if (civInfo->bTurningLeft || civInfo->bTurningRight)
	{
		if (ABS_TIME_PASSED(civInfo->turningTimer, s_fTurnLingerTime))
		{
			if(civInfo->bTurningLeft)
			{
				gclCivilian_TurnOnLeftTurnSignal(e, civInfo, false);
			}
			else
			{
				gclCivilian_TurnOnRightTurnSignal(e, civInfo, false);
			}
		}
	}
	else if (fCurrentSpeed < 2.f)
	{
		civInfo->bCheckTurningVec = false;
	}
	else if (civInfo->bCheckTurningVec)
	{
		F32 fAngleDiff = getAngleBetweenNormalizedVec3(civInfo->vLastDir, vCurrentDir);
		if (fAngleDiff > s_fTurningSuspectThreshold)
		{
			fAngleDiff = getAngleBetweenNormalizedVec3(civInfo->vLastTurnDir, vCurrentDir);
			if (fAngleDiff > s_fTurningThreshold)
			{
				Vec3 vCross;
				crossVec3(civInfo->vLastDir, vCurrentDir, vCross);
				if (vCross[1] < 0.f)
				{
					gclCivilian_TurnOnLeftTurnSignal(e, civInfo, true);
				}
				else
				{
					gclCivilian_TurnOnRightTurnSignal(e, civInfo, true);
				}
			}
		}
		else if (ABS_TIME_PASSED(civInfo->turningTimer, s_fTurnCheckingTime))
		{
			civInfo->bCheckTurningVec = false;
		}
	}
	else
	{ 
		// 
		F32 fAngleDiff = getAngleBetweenNormalizedVec3(civInfo->vLastDir, vCurrentDir);
		if (fAngleDiff > s_fTurningThreshold)
		{
			Vec3 vCross;
			crossVec3(civInfo->vLastDir, vCurrentDir, vCross);
			if (vCross[1] < 0.f)
			{
				gclCivilian_TurnOnLeftTurnSignal(e, civInfo, true);
			}
			else
			{
				gclCivilian_TurnOnRightTurnSignal(e, civInfo, true);
			}
		}
		else if (fAngleDiff > s_fTurningSuspectThreshold)
		{
			civInfo->bCheckTurningVec = true;
			copyVec3(vCurrentDir, civInfo->vLastTurnDir);
			civInfo->turningTimer = ABS_TIME;
		}
	}


	civInfo->fLastSpeed = fCurrentSpeed;
	if (fCurrentSpeed > 2.f)
		copyVec3(vCurrentDir, civInfo->vLastDir);
}


// ------------------------------------------------------------------------------------------------------------------

// receiving requested data from the client about the civilian map def
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9);
void gclCivReceiveClientMapDefInfo(AICivClientMapDefInfo *pLegDefInfo)
{
	eaDestroy(&s_civEditorInfo.civLegDefInfo.eapcLegDefNames);

	if (pLegDefInfo)
	{
		StructCopyAll(parse_AICivClientMapDefInfo, pLegDefInfo, &s_civEditorInfo.civLegDefInfo);
		eaDestroy(&pLegDefInfo->eapcLegDefNames);
	}
	
	s_civEditorInfo.civLegDefInfoDirty = false;
	s_civEditorInfo.civLegDefSentReq = true;

}

// ------------------------------------------------------------------------------------------------------------------

// when the server's map def info changes, server will notify us if we've been editing
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9);
void gclCivNotifyDirtyMapDefInfo(void)
{
	s_civEditorInfo.civLegDefInfoDirty = true;
	s_civEditorInfo.civLegDefSentReq = false;
}

// ------------------------------------------------------------------------------------------------------------------
const char** gclCivilian_GetLegDefNames()
{
	if (s_civEditorInfo.civLegDefInfoDirty)
	{
		if (!s_civEditorInfo.civLegDefSentReq && !isProductionMode())
		{
			s_civEditorInfo.civLegDefSentReq = true;
			ServerCmd_aiCivEditing_RequestCivClientMapDefInfo();
		}
		return NULL;
	}

	return s_civEditorInfo.civLegDefInfo.eapcLegDefNames;
}


// ------------------------------------------------------------------------------------------------------------------
// GCL Civilian Debugging editor-y stuff
// ------------------------------------------------------------------------------------------------------------------
// todo: move this to its own file
#include "GameClientLib.h"
#include "gclEntity.h"
#include "gclWorldDebug.h"
#include "GfxCamera.h"

#include "UIButton.h"
#include "UIDialog.h"
#include "UICheckButton.h"
#include "UIPane.h"
#include "UIWindow.h"
#include "UITextArea.h"
#include "UILabel.h"
#include "cmdParse.h"
#include "UIExpander.h"
#include "EditorManager.h"
#include "EString.h"
typedef struct EMPanel EMPanel;

typedef struct CivilianEditorGUI
{
	UIWindow		*pWindow;
	UIExpanderGroup	*pExpGroup;

	// regen 
	UIExpander		*pExp_Regen;
	UIPane			*pPane_Regen;
	UIButton		*pBtn_RegenLocal;
	UICheckButton	*pCheckBtn_VolumesOnly;
	UICheckButton	*pCheckBtn_PopulatePostRegen;
	UICheckButton	*pCheckBtn_SkipLegSplit;
	UISlider		*pSlider_RegenArea;
	UILabel			*pLabel_RegenArea;

	// debug drawing
	UIExpander		*pExp_DebugDrawing;
	UIButton		*pBtn_SendCivLegs;
	UIButton		*pBtn_SendPathPoints;
	UIButton		*pBtn_SendPOI;
	UIButton		*pBtn_ClearDebugDraw;
		
	// error searching
	UIExpander		*pExp_Errors;
	UIButton		*pBtn_NextError;
	UIButton		*pBtn_PrevError;
	UIButton		*pBtn_RequestProblemLocs;

	UIPane			*pPane_Status;
	UILabel			*pLabel_Status;
	
	// set count
	UIExpander		*pExp_SetCount;

	UILabel			*pLabel_SetPedCount;
	UITextEntry		*pText_SetPedCount;
	UIButton		*pBtn_SetPedCount;

	UILabel			*pLabel_SetCarCount;
	UITextEntry		*pText_SetCarCount;
	UIButton		*pBtn_SetCarCount;

	UILabel			*pLabel_SetTrolleyCount;
	UITextEntry		*pText_SetTrolleyCount;
	UIButton		*pBtn_SetTrolleyCount;
	

	S32 bRegen_VolumesOnly;
	S32 bRegen_SkipLegSplit;
	S32 bRegen_PostPopulate;

	U32	bSentRegenRequest : 1;
	
} CivilianEditorGUI;


CivilianEditorGUI g_civGUI = {0};

static void gclCivEditor_Init(void);
static void gclCivEditor_Destroy();
static void gclCivEditor_SetStatusLabel(const char *pszText);

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(civEditor);
void gclCivEditor(void)
{
	if(!g_civGUI.pWindow)
	{
		gclCivEditor_Init();
	}
	else 
	{
		if (!ui_WindowIsVisible(g_civGUI.pWindow))
		{
			ui_WindowShow(g_civGUI.pWindow);
		}
		else
		{
			gclCivEditor_Destroy();
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_PRIVATE ACMD_CLIENTCMD ACMD_ACCESSLEVEL(9);
void gclCivEditor_NotifyComplete(AICivRegenReport *pReport)
{
	char buffer[256] = {0};

	g_civGUI.bSentRegenRequest = false;

	if (pReport->problemAreaRequest)
	{
		sprintf(buffer, "Problem Locations Found: %d\n", 
						eaSize(&pReport->eaProblemLocs));
	}
	else
	{
		sprintf(buffer, "Regenerating Local Legs... DONE!\n"
						"Created %d legs.\n"
						"Problem Locations Found: %d", 
						pReport->totalLegsCreated, 
						eaSize(&pReport->eaProblemLocs));
	}

	gclCivEditor_SetStatusLabel(buffer);

	
	StructDeInit(parse_AICivRegenReport, &s_civEditorInfo.regenReport);
	memcpy(&s_civEditorInfo.regenReport, pReport, sizeof(AICivRegenReport));
	s_civEditorInfo.curErrorIdx = -1;	

	ZeroStruct(pReport);
}

// ------------------------------------------------------------------------------------------------------------------
bool gclCivEditor_Close(UIAnyWidget *widget, UserData data)
{
	if (g_civGUI.pWindow)
	{
		ui_WindowHide(g_civGUI.pWindow);
		//gclCivEditor_Destroy();	
	}
	
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static void gclCivEditor_Destroy()
{
	ui_WidgetQueueFree(UI_WIDGET(g_civGUI.pWindow));
	
	ZeroStruct(&g_civGUI);
	
}


// ------------------------------------------------------------------------------------------------------------------
static void gclCivEditor_RegenVolumeCheckbox(UIAnyWidget *widget, UserData data)
{
	S32 btnState = !!ui_CheckButtonGetState((UICheckButton*)widget);

	if (widget == UI_WIDGET(g_civGUI.pCheckBtn_VolumesOnly) )
	{
		g_civGUI.bRegen_VolumesOnly = btnState;
	}
	else if (widget == UI_WIDGET(g_civGUI.pCheckBtn_PopulatePostRegen) )
	{
		g_civGUI.bRegen_PostPopulate = btnState;
		
	}
	else
	{
		g_civGUI.bRegen_SkipLegSplit = btnState;
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void gclCivEditor_RegenLocal(UIAnyWidget *widget, UserData data)
{
	AICivRegenOptions	regenOptions = {0};

	// get the proper position 
	Entity *e = entActivePlayerPtr();
		
	if (g_civGUI.bSentRegenRequest == true)
	{
		// pop a window saying that we are currently waiting for the server
		// we may need a way to clear this flag incase that the server crashes 
		// or something else goes "wrong"
		ui_DialogPopup("Regenerating Civilian Legs", 
						"The server is currently busy regnerating legs."
						" Please wait until the process has finished."
						"\nIf the server is not performing leg generation (something is bugged),"
						"please close and reopen the civilian editor window to reset.");
		return;
	}

	if (!e)
	{
		// pop an error window that we couldn't find the active entity
		ui_DialogPopup("Error", "Could not find the active player entity.");
		return;
	}
	
	// todo: get the current camera position if we are detached from the player. 
	if (!gGCLState.bUseFreeCamera)
	{
		entGetPos(e, regenOptions.vRegenPos);
	}
	else
	{
		gfxGetActiveCameraPos(regenOptions.vRegenPos);
	}
	

	// 
	regenOptions.bVolumeLegsOnly = g_civGUI.bRegen_VolumesOnly;
	regenOptions.bSkipLegSplit = g_civGUI.bRegen_SkipLegSplit;
	regenOptions.bPostPopulate = g_civGUI.bRegen_PostPopulate;
	regenOptions.fAreaRegen = s_civEditorInfo.fAreaRegen;

	ServerCmd_acgRegenLocalLegsEx(&regenOptions);
	
	worldDebugClear();


	gclCivEditor_SetStatusLabel("Regenerating Local Legs...\nPlease wait...");
	g_civGUI.bSentRegenRequest = true;
}

// ------------------------------------------------------------------------------------------------------------------
static void gclCivEditor_DebugDraw(UIAnyWidget *widget, UserData data)
{
	if (g_civGUI.bSentRegenRequest == true)
		return; // silently fail for now, we're waiting for the server to generate

	if (widget == UI_WIDGET(g_civGUI.pBtn_SendCivLegs) )
	{
		worldDebugClear();
		globCmdParsef("sendCivLegs");
	}
	else if (widget == UI_WIDGET(g_civGUI.pBtn_SendPathPoints) )
	{
		globCmdParsef("acgSendPathPoints");
	}
	else if (widget == UI_WIDGET(g_civGUI.pBtn_SendPOI) )
	{
		globCmdParsef("aiCivSendPOI");
	}	
	else
	{
		worldDebugClear();
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void gclCivEditor_NextPrevError(UIAnyWidget *widget, UserData data)
{
	S32 numErrors;
	if (widget == UI_WIDGET(g_civGUI.pBtn_RequestProblemLocs) )
	{
		ServerCmd_aiCivSendProblemAreas();
		return;
	}

	numErrors = eaSize(&s_civEditorInfo.regenReport.eaProblemLocs);
	if (numErrors == 0)
	{
		// say that there are no errors
		return;
	}

	if (widget == UI_WIDGET(g_civGUI.pBtn_NextError) )
	{
		s_civEditorInfo.curErrorIdx++;
		if (s_civEditorInfo.curErrorIdx >= numErrors)
		{
			s_civEditorInfo.curErrorIdx = 0;
		}
	}
	else if (widget == UI_WIDGET(g_civGUI.pBtn_PrevError) )
	{
		s_civEditorInfo.curErrorIdx--;
		if (s_civEditorInfo.curErrorIdx < 0)
		{
			s_civEditorInfo.curErrorIdx = numErrors - 1;
		}
	}
	
	{
		char *estr = NULL;
		AICivProblemLocation *pLoc = s_civEditorInfo.regenReport.eaProblemLocs[s_civEditorInfo.curErrorIdx];
		Vec3 camPyr = {0};
		Vec3 vCamPos;
		Vec3 arm, dir;
		F32 fDist;

		fDist = 30.f + pLoc->fProblemAreaSize;
		MIN1(fDist, 200.f);

		setVec3(arm, 0.f, fDist, 10.f);
		
		addVec3(pLoc->vPos, arm, vCamPos);
		subVec3(vCamPos, pLoc->vPos, dir);
		camPyr[0] = DEG(getVec3Pitch(dir));
		camPyr[1] = DEG(getVec3Yaw(dir));
		
		gclSetFreeCamera(true);
		globCmdParsef("setCamPos %f %f %f", vecParamsXYZ(vCamPos));
		globCmdParsef("setcampyr %f %f %f", vecParamsXYZ(camPyr));
				
		estrStackCreate(&estr);
		estrPrintf(&estr, "Error %d of %d\n%s", 
						s_civEditorInfo.curErrorIdx + 1, numErrors, 
						pLoc->pchReason ? pLoc->pchReason : "(null)" );
		gclCivEditor_SetStatusLabel(estr);
		estrDestroy(&estr);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void ui_PaneResizeToChildren(UIWidget *pWidget, F32 widthBuffer, F32 heightBuffer)
{
	
	if (heightBuffer >= 0.f)
	{
		F32 height = ui_WidgetCalcHeight(pWidget) + widthBuffer;
		ui_WidgetSetHeight(pWidget, height);
	}
	if (widthBuffer >= 0.f)
	{
		F32 width = ui_WidgetCalcWidth(pWidget) + widthBuffer;
		ui_WidgetSetWidth(pWidget, width);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void gclCivEditor_SetCivCount(UIAnyWidget *widget, UserData data)
{
	S32 civType = 0;
	U32 numCivs = 0;
	if (widget == UI_WIDGET(g_civGUI.pBtn_SetPedCount))
	{
		const char *pTxt = ui_TextEntryGetText(g_civGUI.pText_SetPedCount);
		numCivs = atoi(pTxt);
		numCivs = CLAMP(numCivs, 0, 2000);
		civType = 0;
	}
	else if (widget == UI_WIDGET(g_civGUI.pBtn_SetCarCount))
	{
		const char *pTxt = ui_TextEntryGetText(g_civGUI.pText_SetCarCount);
		numCivs = atoi(pTxt);
		numCivs = CLAMP(numCivs, 0, 500);
		civType = 1;
	}
	else if (widget == UI_WIDGET(g_civGUI.pBtn_SetTrolleyCount))
	{
		const char *pTxt = ui_TextEntryGetText(g_civGUI.pText_SetTrolleyCount);
		numCivs = atoi(pTxt);
		numCivs = CLAMP(numCivs, 0, 50);
		civType = 2;
	}
	else
	{
		return;
	}

	globCmdParsef("aiCivSetCount %d %d", civType, numCivs);
}

// ------------------------------------------------------------------------------------------------------------------
static void areaRegenSlider_SetStatusLabel()
{
	char text[MAX_PATH];
	sprintf(text, "Dist %.0f", s_civEditorInfo.fAreaRegen);
	ui_LabelSetText(g_civGUI.pLabel_RegenArea, text);
}

// ------------------------------------------------------------------------------------------------------------------
void gclCivEditor_AreaRegenSlider(UIAnyWidget *p, bool bFinished, UserData dat)
{
	UISlider* pSlider = (UISlider*)p;

	F64 ret = ui_SliderGetValue(pSlider);

	s_civEditorInfo.fAreaRegen = ret;
	areaRegenSlider_SetStatusLabel();
}

// ------------------------------------------------------------------------------------------------------------------
void gclCivEditor_GroupExpand(UIAnyWidget *pExpander, UserData data)
{	
	F32 y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pExpGroup));// + UI_STEP * 4.f;

	ui_WidgetSetPosition(UI_WIDGET(g_civGUI.pPane_Status), 5.f, y_pos);
	ui_PaneResizeToChildren(UI_WIDGET(g_civGUI.pWindow), -1.f, UI_STEP * 4.f);
	ui_WindowShow(g_civGUI.pWindow);
}

#define STATUS_PANE_WIDTH	340
#define STATUS_PANE_DEFAULT_HEIGHT	60
// ------------------------------------------------------------------------------------------------------------------
static void gclCivEditor_SetStatusLabel(const char *pszText)
{
	ui_LabelSetText(g_civGUI.pLabel_Status, pszText);
	ui_LabelSetWordWrap(g_civGUI.pLabel_Status, true);
	ui_LabelUpdateDimensionsForWidth(g_civGUI.pLabel_Status, STATUS_PANE_WIDTH);
}


// ------------------------------------------------------------------------------------------------------------------
static void gclCivEditor_Init()
{
	g_civGUI.bRegen_VolumesOnly = false;
	g_civGUI.bRegen_SkipLegSplit = false;
	g_civGUI.bRegen_PostPopulate = true;

	g_civGUI.pWindow = ui_WindowCreate("Civilian Editor", 10, 10, 350, 200);

	ui_WindowSetCloseCallback(g_civGUI.pWindow, gclCivEditor_Close, NULL);

	ui_WindowSetResizable(g_civGUI.pWindow, false);
	
	// setup the expansion group
	g_civGUI.pExpGroup = ui_ExpanderGroupCreate();
	ui_WidgetSetDimensionsEx((UIWidget*) g_civGUI.pExpGroup, 
								1, g_civGUI.pExpGroup->widget.height, 
								UIUnitPercentage, UIUnitFixed);
	ui_WidgetSetPosition((UIWidget*) g_civGUI.pExpGroup, 0.f, 0.f);
	ui_ExpanderGroupSetGrow(g_civGUI.pExpGroup, true);
	ui_WindowAddChild(g_civGUI.pWindow, g_civGUI.pExpGroup);

	
	// Set up buttons
	{
		const F32 y_pad = UI_STEP * .5f;
		/*
		const F32 x_align = 5.f;
		const F32 y_start = 5.f;
		
		F32 y_pos = y_start;
		F32 x_pos = x_align;*/
		
		
		// regen group
		{
			#define	REGEN_UPPER_LEFT_Y	4.f
			
			g_civGUI.pExp_Regen = ui_ExpanderCreate("Generation", 50.f);
			ui_ExpanderGroupAddExpander(g_civGUI.pExpGroup, g_civGUI.pExp_Regen);
			ui_ExpanderSetOpened(g_civGUI.pExp_Regen, true);
			ui_ExpanderSetExpandCallback(g_civGUI.pExp_Regen, gclCivEditor_GroupExpand, NULL);

			{
				F32 y_pos, x_pos;

				g_civGUI.pBtn_RegenLocal = ui_ButtonCreate("Regen Local", 0.f, REGEN_UPPER_LEFT_Y, NULL, NULL);
				ui_ButtonSetUpCallback(g_civGUI.pBtn_RegenLocal, gclCivEditor_RegenLocal, NULL);

				y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pBtn_RegenLocal)) + y_pad;
				x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pBtn_RegenLocal)) + UI_STEP;

				// regen options
				{
					g_civGUI.pCheckBtn_VolumesOnly = ui_CheckButtonCreate(x_pos, REGEN_UPPER_LEFT_Y, "Volumes Only", false);
					ui_CheckButtonSetToggledCallback(g_civGUI.pCheckBtn_VolumesOnly, gclCivEditor_RegenVolumeCheckbox, NULL);
					ui_CheckButtonSetState(g_civGUI.pCheckBtn_VolumesOnly, g_civGUI.bRegen_VolumesOnly);

					x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pCheckBtn_VolumesOnly)) + UI_STEP;

					g_civGUI.pCheckBtn_SkipLegSplit = ui_CheckButtonCreate(x_pos, REGEN_UPPER_LEFT_Y, "Skip Leg Split", false);
					ui_CheckButtonSetToggledCallback(g_civGUI.pCheckBtn_SkipLegSplit, gclCivEditor_RegenVolumeCheckbox, NULL);
					ui_CheckButtonSetState(g_civGUI.pCheckBtn_SkipLegSplit, g_civGUI.bRegen_SkipLegSplit);

					x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pBtn_RegenLocal)) + UI_STEP;
					y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pBtn_RegenLocal)) + y_pad;
					
					g_civGUI.pCheckBtn_PopulatePostRegen = ui_CheckButtonCreate(x_pos, y_pos, "Post Populate Civs", false);
					ui_CheckButtonSetToggledCallback(g_civGUI.pCheckBtn_PopulatePostRegen, gclCivEditor_RegenVolumeCheckbox, NULL);
					ui_CheckButtonSetState(g_civGUI.pCheckBtn_PopulatePostRegen, g_civGUI.bRegen_PostPopulate);
				}

				y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pCheckBtn_PopulatePostRegen)) + y_pad;
				#define AREA_REGEN_MIN	300.f
				#define AREA_REGEN_MAX	2000.f
		
				g_civGUI.pLabel_RegenArea = ui_LabelCreate("", 0, y_pos);
				areaRegenSlider_SetStatusLabel();
				x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pLabel_RegenArea)) + UI_STEP;
			
				g_civGUI.pSlider_RegenArea = ui_SliderCreate(x_pos, y_pos, 250, 
															AREA_REGEN_MIN, AREA_REGEN_MAX, 
															s_civEditorInfo.fAreaRegen);
				ui_SliderSetChangedCallback(g_civGUI.pSlider_RegenArea, gclCivEditor_AreaRegenSlider, NULL);
				ui_SliderSetRange(g_civGUI.pSlider_RegenArea, AREA_REGEN_MIN, AREA_REGEN_MAX, 50.f);
				
				y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pSlider_RegenArea)) + y_pad * 2.f;
				ui_ExpanderSetHeight(g_civGUI.pExp_Regen, y_pos);

				ui_ExpanderAddChild(g_civGUI.pExp_Regen, g_civGUI.pBtn_RegenLocal);
				ui_ExpanderAddChild(g_civGUI.pExp_Regen, g_civGUI.pCheckBtn_VolumesOnly);
				ui_ExpanderAddChild(g_civGUI.pExp_Regen, g_civGUI.pCheckBtn_SkipLegSplit);
				ui_ExpanderAddChild(g_civGUI.pExp_Regen, g_civGUI.pCheckBtn_PopulatePostRegen);

				ui_ExpanderAddChild(g_civGUI.pExp_Regen, g_civGUI.pLabel_RegenArea);
				ui_ExpanderAddChild(g_civGUI.pExp_Regen, g_civGUI.pSlider_RegenArea);
			}
		}

		// problem location group
		{
			F32 x_pos, y_pos;

			g_civGUI.pExp_Errors = ui_ExpanderCreate("Problem Locations", 50.f);
			ui_ExpanderGroupAddExpander(g_civGUI.pExpGroup, g_civGUI.pExp_Errors);
			ui_ExpanderSetOpened(g_civGUI.pExp_Errors, true);
			ui_ExpanderSetExpandCallback(g_civGUI.pExp_Errors, gclCivEditor_GroupExpand, NULL);


			g_civGUI.pBtn_PrevError = ui_ButtonCreate("PrevError", 0, 0, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_PrevError, gclCivEditor_NextPrevError, NULL);
			x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pBtn_PrevError)) + y_pad;

			g_civGUI.pBtn_NextError = ui_ButtonCreate("NextError", x_pos, 0, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_NextError, gclCivEditor_NextPrevError, NULL);
			y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pBtn_PrevError)) + y_pad;

			g_civGUI.pBtn_RequestProblemLocs = ui_ButtonCreate("Request Problem Locs", 0, y_pos, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_RequestProblemLocs , gclCivEditor_NextPrevError, NULL);

			y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pBtn_RequestProblemLocs)) + y_pad * 2.f;
			ui_ExpanderSetHeight(g_civGUI.pExp_Errors, y_pos);

			ui_ExpanderAddChild(g_civGUI.pExp_Errors, g_civGUI.pBtn_NextError);
			ui_ExpanderAddChild(g_civGUI.pExp_Errors, g_civGUI.pBtn_PrevError);
			ui_ExpanderAddChild(g_civGUI.pExp_Errors, g_civGUI.pBtn_RequestProblemLocs);
			
		}

		// Debug Drawing group
		{
			F32 x_pos, y_pos;
			
			g_civGUI.pExp_DebugDrawing = ui_ExpanderCreate("Drawing", 50.f);
			ui_ExpanderGroupAddExpander(g_civGUI.pExpGroup, g_civGUI.pExp_DebugDrawing);
			ui_ExpanderSetOpened(g_civGUI.pExp_DebugDrawing, true);
			ui_ExpanderSetExpandCallback(g_civGUI.pExp_DebugDrawing, gclCivEditor_GroupExpand, NULL);

			g_civGUI.pBtn_SendCivLegs = ui_ButtonCreate("Show Legs", 0, 0, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_SendCivLegs, gclCivEditor_DebugDraw, NULL);
			x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pBtn_SendCivLegs)) + y_pad;

			g_civGUI.pBtn_SendPathPoints = ui_ButtonCreate("Show Path Points", x_pos, 0, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_SendPathPoints, gclCivEditor_DebugDraw, NULL);
			x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pBtn_SendPathPoints)) + y_pad;

			g_civGUI.pBtn_SendPOI = ui_ButtonCreate("Show POIs", x_pos, 0, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_SendPOI, gclCivEditor_DebugDraw, NULL);
			y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pBtn_SendPOI)) + y_pad;

			g_civGUI.pBtn_ClearDebugDraw = ui_ButtonCreate("Clear Debug Drawing", 0, y_pos, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_ClearDebugDraw, gclCivEditor_DebugDraw, NULL);
			
			ui_ExpanderAddChild(g_civGUI.pExp_DebugDrawing, g_civGUI.pBtn_SendCivLegs);
			ui_ExpanderAddChild(g_civGUI.pExp_DebugDrawing, g_civGUI.pBtn_SendPathPoints);
			ui_ExpanderAddChild(g_civGUI.pExp_DebugDrawing, g_civGUI.pBtn_SendPOI);
			ui_ExpanderAddChild(g_civGUI.pExp_DebugDrawing, g_civGUI.pBtn_ClearDebugDraw);
			
			y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pBtn_ClearDebugDraw)) + y_pad * 2.f;
			ui_ExpanderSetHeight(g_civGUI.pExp_DebugDrawing, y_pos);
		}

		{
			F32 x_pos, y_pos;
			F32 labelWidth;

			g_civGUI.pExp_SetCount = ui_ExpanderCreate("Set Counts", 50.f);
			ui_ExpanderGroupAddExpander(g_civGUI.pExpGroup, g_civGUI.pExp_SetCount);
			ui_ExpanderSetOpened(g_civGUI.pExp_SetCount, false);
			ui_ExpanderSetExpandCallback(g_civGUI.pExp_SetCount, gclCivEditor_GroupExpand, NULL);

			y_pos = 0;
			// pedestrian
			g_civGUI.pLabel_SetPedCount = ui_LabelCreate("Pedestrian", 0, y_pos);
			x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pLabel_SetPedCount)) + y_pad;
			labelWidth = ui_WidgetGetWidth(UI_WIDGET(g_civGUI.pLabel_SetPedCount));
			
			g_civGUI.pText_SetPedCount = ui_TextEntryCreate("0", x_pos, y_pos);
			ui_TextEntrySetIntegerOnly(g_civGUI.pText_SetPedCount);
			x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pText_SetPedCount)) + y_pad;

			g_civGUI.pBtn_SetPedCount = ui_ButtonCreate("Set Count", x_pos, y_pos, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_SetPedCount, gclCivEditor_SetCivCount, NULL);

			// car
			y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pLabel_SetPedCount)) + y_pad;
			
			g_civGUI.pLabel_SetCarCount = ui_LabelCreate("Car", 0, y_pos);
			ui_WidgetSetWidth(UI_WIDGET(g_civGUI.pLabel_SetCarCount), labelWidth);
			x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pLabel_SetCarCount)) + y_pad;
			
			g_civGUI.pText_SetCarCount = ui_TextEntryCreate("0", x_pos, y_pos);
			ui_TextEntrySetIntegerOnly(g_civGUI.pText_SetCarCount);
			x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pText_SetCarCount)) + y_pad;

			g_civGUI.pBtn_SetCarCount = ui_ButtonCreate("Set Count", x_pos, y_pos, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_SetCarCount, gclCivEditor_SetCivCount, NULL);
			
			
			// trolley
			y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pLabel_SetCarCount)) + y_pad;
			
			g_civGUI.pLabel_SetTrolleyCount = ui_LabelCreate("Trolley", 0, y_pos);
			ui_WidgetSetWidth(UI_WIDGET(g_civGUI.pLabel_SetTrolleyCount), labelWidth);
			x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pLabel_SetTrolleyCount)) + y_pad;
			
			g_civGUI.pText_SetTrolleyCount = ui_TextEntryCreate("0", x_pos, y_pos);
			ui_TextEntrySetIntegerOnly(g_civGUI.pText_SetTrolleyCount);
			x_pos = ui_WidgetGetNextX(UI_WIDGET(g_civGUI.pText_SetTrolleyCount)) + y_pad;

			g_civGUI.pBtn_SetTrolleyCount = ui_ButtonCreate("Set Count", x_pos, y_pos, NULL, NULL);
			ui_ButtonSetUpCallback(g_civGUI.pBtn_SetTrolleyCount, gclCivEditor_SetCivCount, NULL);

			ui_ExpanderAddChild(g_civGUI.pExp_SetCount, g_civGUI.pLabel_SetPedCount);
			ui_ExpanderAddChild(g_civGUI.pExp_SetCount, g_civGUI.pText_SetPedCount);
			ui_ExpanderAddChild(g_civGUI.pExp_SetCount, g_civGUI.pBtn_SetPedCount);

			ui_ExpanderAddChild(g_civGUI.pExp_SetCount, g_civGUI.pLabel_SetCarCount);
			ui_ExpanderAddChild(g_civGUI.pExp_SetCount, g_civGUI.pText_SetCarCount);
			ui_ExpanderAddChild(g_civGUI.pExp_SetCount, g_civGUI.pBtn_SetCarCount);

			ui_ExpanderAddChild(g_civGUI.pExp_SetCount, g_civGUI.pLabel_SetTrolleyCount);
			ui_ExpanderAddChild(g_civGUI.pExp_SetCount, g_civGUI.pText_SetTrolleyCount);
			ui_ExpanderAddChild(g_civGUI.pExp_SetCount, g_civGUI.pBtn_SetTrolleyCount);
			
			y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pBtn_SetTrolleyCount)) + y_pad * 2.f;
			ui_ExpanderSetHeight(g_civGUI.pExp_SetCount, y_pos);
		}
		
		
		// status pane
		{
			//y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pBtn_ClearDebugDraw)) + UI_STEP * 4.f;
			F32 y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pExpGroup));// + UI_STEP * 4.f;
			

			g_civGUI.pPane_Status = ui_PaneCreate(5.f, y_pos, 
												STATUS_PANE_WIDTH, STATUS_PANE_DEFAULT_HEIGHT, 
												UIUnitFixed, UIUnitFixed, false);
			y_pos = ui_WidgetGetNextY(UI_WIDGET(g_civGUI.pPane_Status)) + (y_pad * 2.f);

			{
				ui_WindowAddChild(g_civGUI.pWindow, g_civGUI.pPane_Status);
				g_civGUI.pLabel_Status = ui_LabelCreate("", 0, 0);

				ui_PaneAddChild(g_civGUI.pPane_Status, g_civGUI.pLabel_Status);
			}
		}
	}

	ui_PaneResizeToChildren(UI_WIDGET(g_civGUI.pWindow), -1.f, UI_STEP * 4.f);
	ui_WindowShow(g_civGUI.pWindow);
}
			
#include "gclCivilian_c_ast.c"
