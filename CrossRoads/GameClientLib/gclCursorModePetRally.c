#include "gclCamera.h"
#include "gclCursorMode.h"
#include "gclEntity.h"
#include "gclMapState.h"
#include "GfxCamera.h"
#include "ClientTargeting.h"
#include "dynFxInterface.h"
#include "dynFxManager.h"
#include "Character.h"
#include "Character_target.h"
#include "mapstate_common.h"
#include "Player.h"

#include "inputLib.h"
#include "inputMouse.h"
#include "UICore.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"

#include "AutoGen/GameServerLib_autogen_ServerCmdWrappers.h"
#include "gclCursorModePetRally_c_ast.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_UISystem););

static void gclCursorPetRally_OnClick(bool bDown);
static void gclCursorPetRally_OnModeEnter();
static void gclCursorPetRally_OnModeExit();
static void gclCursorPetRally_Update();

void PetCommands_SetRallyPoint(Entity *pOwner, EntityRef erPet, const Vec3 vPosition, const Vec3 vNormal);
void PetCommands_SetStateByEnt(SA_PARAM_OP_VALID Entity* ent, const char* state);
void PetCommands_SetTeamRallyPoint(Entity *pOwner, const Vec3 vPosition);

#define PETRALLY_CURSOR_MODE_NAME	"petRally"

AUTO_STRUCT;
typedef struct CursorModePetRallyDef
{
	const char *pchCursorModeName;				AST(ADDNAMES("CursorModeName:") POOL_STRING)
	const char *pchCursorModeUnplaceableName;	AST(ADDNAMES("CursorModeUnplaceableName:") POOL_STRING)
	const char *pchRallyDragPlacementFXName;	AST(ADDNAMES("RallyDragPlacementFXName:") POOL_STRING)
	const char *pchRallyPointFXBaseName;		AST(ADDNAMES("RallyPointFXBaseName:") POOL_STRING)
} CursorModePetRallyDef;

typedef struct CursorModePetRallyRuntime
{
	EntityRef currentPetEnt;
	Vec3 vPosition;
	dtFx hRallyPointFx;
	dtNode hRallyPointNode;
} CursorModePetRallyRuntime;

static CursorModePetRallyRuntime s_petRallyRuntime = {0};
static CursorModePetRallyDef s_petRallyDef = {0};

const char* gclCursorPetRally_GetRallyPointFXBaseName()
{
	return s_petRallyDef.pchRallyPointFXBaseName;
}



AUTO_STARTUP(PetRally);
void CursorMode_PetRallyInitialize(void)
{
	StructInit(parse_CursorModePetRallyDef, &s_petRallyDef);

	ParserLoadFiles(NULL, "defs/config/PetRallyCursorMode.def","PetRallyCursorMode.bin",PARSER_OPTIONALFLAG,parse_CursorModePetRallyDef,&s_petRallyDef);
		
	gclCursorMode_Register(PETRALLY_CURSOR_MODE_NAME, s_petRallyDef.pchCursorModeName,
							gclCursorPetRally_OnClick, 
							gclCursorPetRally_OnModeEnter,
							gclCursorPetRally_OnModeExit,
							gclCursorPetRally_Update);
}




AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorPetRally_BeginPlaceForEntity");
void gclCursorPetRally_BeginPlaceForEntity(SA_PARAM_OP_VALID Entity *pPetEnt)
{
	Entity *e = entActivePlayerPtr();

	if (!e) 
		return;

	gclCursorMode_SetModeByName(PETRALLY_CURSOR_MODE_NAME);
	// set up the current pet after we enter the mode since it will clear on entering
	s_petRallyRuntime.currentPetEnt = pPetEnt ? entGetRef(pPetEnt) : 0;
}

AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(CursorPetRally_BeginPlaceForEntity);
void gclCmdCursorPetRally_BeginPlaceForEntity(U32 iEntRef)
{
	Entity* pEnt = entFromEntityRef(PARTITION_CLIENT, iEntRef);
	gclCursorPetRally_BeginPlaceForEntity(pEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorPetRally_BeginPlaceForTeam");
void gclCursorPetRally_BeginPlaceForTeam()
{
	Entity *e = entActivePlayerPtr();

	if (!e) 
		return;

	gclCursorMode_SetModeByName(PETRALLY_CURSOR_MODE_NAME);
	s_petRallyRuntime.currentPetEnt = 0;
}


AUTO_COMMAND ACMD_ACCESSLEVEL(0) ACMD_NAME(CursorPetRally_BeginPlaceForTeam);
void gclCmdCursorPetRally_BeginPlaceForTeam(void)
{
	gclCursorPetRally_BeginPlaceForTeam();
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorPetRally_IsInModeForEntity");
bool gclCursorPetRally_IsInModeForEntity(SA_PARAM_OP_VALID Entity *e)
{
	return e && entGetRef(e) == s_petRallyRuntime.currentPetEnt;
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorPetRally_IsInModeForTeam");
bool gclCursorPetRally_IsInModeForTeam()
{
	return !s_petRallyRuntime.currentPetEnt && !stricmp(gclCursorMode_GetCurrent(), PETRALLY_CURSOR_MODE_NAME);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorPetRally_GetSelectedEntity");
SA_RET_OP_VALID Entity *gclCursorPetRally_GetSelectedEntity(void)
{
	return entFromEntityRefAnyPartition(s_petRallyRuntime.currentPetEnt);
}

AUTO_EXPR_FUNC(entityutil) ACMD_NAME("CursorPetRally_ClearSelectedEntity");
void gclCursorPetRally_ClearSelectedEntity(void)
{
	s_petRallyRuntime.currentPetEnt = 0;
}

static void gclCursorPetRally_OnModeEnter(void)
{
	if (s_petRallyDef.pchRallyDragPlacementFXName)
	{
		s_petRallyRuntime.hRallyPointNode = dtNodeCreate();
	}
	s_petRallyRuntime.currentPetEnt = 0;
	entity_SetTarget(entActivePlayerPtr(), 0);
}

static void gclCursorPetRally_OnModeExit(void)
{
	if (s_petRallyRuntime.hRallyPointFx)
	{
		dtFxKill(s_petRallyRuntime.hRallyPointFx);
		s_petRallyRuntime.hRallyPointFx = 0;
	}

	if(s_petRallyRuntime.hRallyPointNode)
	{
		dtNodeDestroy(s_petRallyRuntime.hRallyPointNode);
		s_petRallyRuntime.hRallyPointNode = 0;
	}

	s_petRallyRuntime.currentPetEnt = 0;
}

static bool _getRallyPointPosition(const Vec3 rayStart, const Vec3 rayDir, Vec3 vOutPos, Vec3 vOutNormal)
{
	WorldCollCollideResults results = {0};
	Vec3 vRayEnd;

	scaleAddVec3(rayDir, 200.f, rayStart, vRayEnd);

	if (!worldCollideRay(PARTITION_CLIENT, rayStart, vRayEnd, WC_FILTER_BIT_MOVEMENT, &results))
		return false;

	// validate this position
	if (getAngleBetweenVec3(upvec, results.normalWorld) > RAD(40.f))
	{
		// the angle is too steep. 
		// step back some feet from the hit position along the ray direction and cast downwards
		Vec3 vRaySt;
		
		scaleAddVec3(rayDir, -4.f, results.posWorldImpact, vRaySt);
		scaleAddVec3(upvec, -20.f, vRaySt, vRayEnd);

		if (!worldCollideRay(PARTITION_CLIENT, vRaySt, vRayEnd, WC_FILTER_BIT_MOVEMENT, &results))
			return false;	
		if (getAngleBetweenVec3(upvec, results.normalWorld) > RAD(40.f))
			return false;
	}

	copyVec3(results.posWorldImpact, vOutPos);
	copyVec3(results.normalWorld, vOutNormal);
	return true;	
}

static void gclCursorPetRally_GetRay(Entity* e, Vec3 rayStart, Vec3 rayDir)
{
	if (!mouseIsLocked())
	{
		target_GetCursorRay(e, rayStart, rayDir);
	}
	else
	{
		GfxCameraController* pCamera = gfxGetActiveCameraController();
		bool bIgnorePitch = false;

		gfxGetActiveCameraPos(rayStart);

		if(!e->pPlayer || !e->pPlayer->bUseFacingPitch)
		{
			bIgnorePitch = true;
		}
		gclCamera_GetFacingDirection(pCamera, bIgnorePitch, rayDir);
	}
}

static void gclCursorPetRally_HandleClick(Entity* e)
{
	Entity *petEnt = NULL;
	Vec3 rayStart, rayDir, vOutPos, vOutNormal;
	MapState *state = mapStateClient_Get();

	gclCursorPetRally_GetRay(e, rayStart, rayDir);

	if (s_petRallyRuntime.currentPetEnt)
		petEnt = entFromEntityRefAnyPartition(s_petRallyRuntime.currentPetEnt);

	if (_getRallyPointPosition(rayStart, rayDir, vOutPos, vOutNormal))
	{
		PetCommands_SetRallyPoint(e, s_petRallyRuntime.currentPetEnt, vOutPos, vOutNormal);

		if (SAFE_MEMBER(state,bPaused))
		{
			gclCursorModeAllowThisClick(true);
		}
		else
		{	// if we aren't paused, exit out of this cursor mode
			gclCursorMode_ChangeToDefault();
		}
	}
}

static void gclCursorPetRally_OnClick(bool bDown)
{
	Entity *e = entActivePlayerPtr();

	if (e && e->pChar && bDown && !inpCheckHandled())
	{
		if (!mouseIsLocked())
		{
			Entity *petEnt = NULL;

			if (petEnt = getEntityUnderMouse(true))
			{
				entity_SetTarget(entActivePlayerPtr(), entGetRef(petEnt));
			}
			else
			{
				gclCursorPetRally_HandleClick(e);
			}
		}
		else if (gclCamera_IsInMode(kCameraMode_ShooterCamera) && !e->pChar->currentTargetRef)
		{
			gclCursorPetRally_HandleClick(e);
		}
	}
}

static void _hideRallyPointFX()
{
	if (s_petRallyRuntime.hRallyPointFx)
	{
		Vec3 z;
		zeroVec3(z);
		dtNodeSetPos(s_petRallyRuntime.hRallyPointNode, z);
	}
}


static void gclCursorPetRally_Update()
{
	bool bValidPlacement = false;
	Entity* e = entActivePlayerPtr();

	if (!e || (mouseIsLocked() && !gclCamera_IsInMode(kCameraMode_ShooterCamera)))
	{
		_hideRallyPointFX();
		return;
	}
	
	if (!inpCheckHandled())
	{
		Vec3 rayStart, rayDir, vOutPos, vOutNormal;

		gclCursorPetRally_GetRay(e, rayStart, rayDir);

		if (_getRallyPointPosition(rayStart, rayDir, vOutPos, vOutNormal))
		{
			Quat qRot;
			unitQuat(qRot);

			if (s_petRallyRuntime.hRallyPointNode && s_petRallyDef.pchRallyDragPlacementFXName)
			{
				dtNodeSetPos(s_petRallyRuntime.hRallyPointNode, vOutPos);
				if (!s_petRallyRuntime.hRallyPointFx)
				{
					DynFxManager *pManager = dynFxGetGlobalFxManager(vOutPos);

					if (pManager)
					{
						Vec3 z;
						zeroVec3(z);
						s_petRallyRuntime.hRallyPointFx = dtAddFx(pManager->guid, 
																	s_petRallyDef.pchRallyDragPlacementFXName, 
																	NULL, 0, s_petRallyRuntime.hRallyPointNode, 
																	1.f, 0, NULL, eDynFxSource_UI, NULL, NULL);
						if (!s_petRallyRuntime.hRallyPointFx)
						{
							// failed to create the FX, destroy the node so we do not try and recreate
							dtNodeDestroy(s_petRallyRuntime.hRallyPointNode);
							s_petRallyRuntime.hRallyPointNode = 0;
						}
					}
				}
			}
			bValidPlacement = true;
		}
	
	}

	if (!bValidPlacement)
	{
		// change the cursor 
		_hideRallyPointFX();
		
		if (s_petRallyDef.pchCursorModeUnplaceableName)
		{
			ui_SetCurrentDefaultCursor(s_petRallyDef.pchCursorModeUnplaceableName);
			ui_SetCursorByName(s_petRallyDef.pchCursorModeUnplaceableName);
		}
	}
	else
	{
		
		if (s_petRallyDef.pchCursorModeName)
		{
			ui_SetCurrentDefaultCursor(s_petRallyDef.pchCursorModeName);
			ui_SetCursorByName(s_petRallyDef.pchCursorModeName);
		}
	}
	
}

#include "gclCursorModePetRally_c_ast.c"
