#include "gclDirectionalIndicatorFX.h"
#include "dynFxManager.h"
#include "dynFxInterface.h"
#include "dynNodeInline.h"
#include "dynFxInfo.h"
#include "mathutil.h"
#include "Entity.h"
#include "gclEntity.h"
#include "mission_common.h"
#include "EntityLib.h"
#include "StringCache.h"
#include "prefs.h"
#include "GameClientLib.h"

#include "gclDirectionalIndicatorFX_c_ast.h"
#include "AutoGen/dynFxInfo_h_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static dtFx s_hDirectionalIndicatorFX;

DirectionalIndicatorFX_State difx_State;
DirectionalIndicatorFX_State difx_DefaultState = 
{ 
	/* .iShow	= */ true, 
	/* .fAlpha	= */ 1.0f,
};

AUTO_STRUCT;
typedef struct DirectionalIndicatorFXConfig 
{
	const char* ArrowFX;
} DirectionalIndicatorFXConfig;

static DirectionalIndicatorFXConfig s_DirectionalIndicatorFXConfig;

bool DirectionalIndicatorFX_Enabled(void)
{
	return s_DirectionalIndicatorFXConfig.ArrowFX && s_DirectionalIndicatorFXConfig.ArrowFX[0];
}

#define SET_DYN_PARAM(pParam, name, setter)			\
	pParam = StructAlloc(parse_DynDefineParam);		\
	pParam->pcParamName = allocAddString(name);		\
	setter;											\
	eaPush(&pParams->eaDefineParams, pParam);

void DirectionalIndicatorFX_Create(const char* pchEffect, const Vec3* pv3Target)
{
	DynFx *pDynFx = NULL;
	DynFxManager *pManager = dynFxGetGlobalFxManager(zerovec3);
	Entity *pEnt = entActivePlayerPtr();
	if (pManager && pEnt && pchEffect)
	{ 
		DynParamBlock *pParams = dynParamBlockCreate();
		DynDefineParam *pParam;
		SET_DYN_PARAM(pParam, "UserAlpha", MultiValSetFloat(&pParam->mvVal, 255 * difx_State.fAlpha));
		SET_DYN_PARAM(pParam, "TargetWaypointLocation", MultiValSetVec3(&pParam->mvVal, pv3Target));
		s_hDirectionalIndicatorFX = dtAddFx(pManager->guid, pchEffect, pParams, 0, pEnt->dyn.guidRoot, 1.f, 0, NULL, eDynFxSource_UI, NULL, NULL);
	}
}

#undef SET_DYN_PARAM

void DirectionalIndicatorFX_Destroy()
{
	if (s_hDirectionalIndicatorFX)
	{
		dtFxKill(s_hDirectionalIndicatorFX);
		s_hDirectionalIndicatorFX = 0;
	}
}

int DirectionalIndicatorFX_WaypointComparitor(const Vec3 v3Center, const MinimapWaypoint **ppA, const MinimapWaypoint **ppB)
{
	F32 fDistA = distance3Squared((*ppA)->pos, v3Center);
	F32 fDistB = distance3Squared((*ppB)->pos, v3Center);
	return (fDistA < fDistB) ? -1 : (fDistA > fDistB) ? 1 : (*ppA - *ppB);
}

void DirectionalIndicatorFX_OncePerFrame()
{
	static MinimapWaypoint **s_eaWaypoints = NULL;
	static Vec3 s_pPreviousLocation;
	static float s_fPreviousAlpha = 0;
	if (difx_State.iShow && !gGCLState.bCutsceneActive)
	{
		// Find the primary mission
		Entity *pEnt = entActivePlayerPtr();
		MissionInfo *pMissionInfo = mission_GetInfoFromPlayer(pEnt);
		const char *pchPrimary = entGetPrimaryMission(pEnt);
		eaClearFast(&s_eaWaypoints);
		if (pchPrimary)
		{
			// Get the list of waypoints for that mission
			Vec3 v3PlayerPos;
			int i;
			for (i = 0; i < eaSize(&pMissionInfo->waypointList); i++)
			{
				MinimapWaypoint *pWaypoint = pMissionInfo->waypointList[i];
				if (pWaypoint->pchMissionRefString && strstri(pWaypoint->pchMissionRefString, pchPrimary))
				{
					eaPush(&s_eaWaypoints, pWaypoint);
				}
			}

			// Find the nearest waypoint
			entGetPos(pEnt, v3PlayerPos);
			eaQSort_s(s_eaWaypoints, DirectionalIndicatorFX_WaypointComparitor, v3PlayerPos);
		}
		// Set the node there
		if (eaSize(&s_eaWaypoints) == 0)
		{
			copyVec3(zerovec3, s_pPreviousLocation);
			DirectionalIndicatorFX_Destroy();
		}
		else if (!sameVec3(s_pPreviousLocation, s_eaWaypoints[0]->pos) || s_fPreviousAlpha != difx_State.fAlpha)
		{
			copyVec3(s_eaWaypoints[0]->pos, s_pPreviousLocation);
			s_fPreviousAlpha = difx_State.fAlpha;
			DirectionalIndicatorFX_Destroy();
			DirectionalIndicatorFX_Create(s_DirectionalIndicatorFXConfig.ArrowFX, &s_eaWaypoints[0]->pos);
		}
	}
	else
	{
		copyVec3(zerovec3, s_pPreviousLocation);
		DirectionalIndicatorFX_Destroy();
	}
}

AUTO_STARTUP(DirectionalIndicatorFX);
void DirectionalIndicatorFX_Startup(void)
{
	difx_State.iShow = GamePrefGetInt("ArrowShow", difx_DefaultState.iShow);
	difx_State.fAlpha = GamePrefGetFloat("ArrowAlpha", difx_DefaultState.fAlpha);
	ParserLoadFiles(NULL, "defs/config/DirectionalIndicatorFX.def", "DirectionalIndicatorFX.bin", PARSER_OPTIONALFLAG, parse_DirectionalIndicatorFXConfig, &s_DirectionalIndicatorFXConfig);
}

#include "gclDirectionalIndicatorFX_c_ast.c"
