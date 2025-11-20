#include "aiCivilianPedestrian.h"
#include "aiAmbient.h"
#include "aiCivMovement.h"
#include "aiCivilianTraffic.h"

#include "aiAnimList.h"
#include "aiConfig.h"
#include "aiDebug.h"
#include "aiFCExprFunc.h"
#include "aiLib.h"
#include "aiJobs.h"

#include "AILib_autogen_QueuedFuncs.h"

#include "aiMovement.h"
#include "aiStruct.h"
#include "AnimList_Common.h"
#include "beacon.h"
#include "../beaconPrivate.h"
#include "../gslPartition.h"
#include "CommandQueue.h"

#include "encounter_common.h"
#include "entCritter.h"
#include "EntityMovementManager.h"
#include "EntityMovementDefault.h"

#include "Expression.h"
#include "GameEvent.h"
#include "gslEntity.h"
#include "gslInteractable.h"
#include "gslMapState.h"
#include "gslMission.h"
#include "interaction_common.h"
#include "PowersMovement.h"
#include "Player.h"
#include "Character.h"
#include "Character_target.h"
#include "rand.h"
#include "StateMachine.h"
#include "wlEncounter.h"
#include "WorldColl.h"
#include "WorldGrid.h"
#include "WorldLib.h"							    
#include "wlVolumes.h"

#define CIV_ANIMBIT_CMDQUEUE_NAME		"civBits"
#define CIV_SCARED_BIT_NAME				"FEAR"
#define CIV_CALLOUT_JOB_NAME			"civCallout"

#define CIVILIAN_ONCLICK_DELAY	6
#define CIVILIAN_EMOTE_DELAY	6
#define CIVILIAN_KILL_CHEER_DELAY		10
#define PERCENTAGE_CHANCE 100.0f
#define CIV_NORMAL_COUNT_THRESHOLD	200


static const F32 s_fPed_UpdatePerSeconds = 1.0f;
static const F32 s_fPed_SurfaceRequesterDelTime = 10.f;

static F32 s_fPedestrianColRadius = 5.f;

static F32 s_fCivPedestrianMinSpeedCrosswalk = 7.0f;
static F32 s_fCivPedestrianSpeedRangeCrosswalk = 3.5f;
static F32 s_fCivMessageDuration = 5.0f;

static F32 s_fCivIdleChance = 0.00666f;
static F32 s_fCivChatterChance = 0.00333f;
static U32 s_iCivDoChatter = 1;
static U32 s_iCivDoWandering = 1;

static const F32 s_fPedestrianOnClickDist = 45.0f;

static int g_disableOnClick = 0;

// Emote reacting vars

AUTO_CMD_INT(g_disableOnClick, aiCivDisableOnClick);

static AICivilianFunctionSet	s_pedestrianFunctionTable;

static AIJobDesc			g_calloutJobDescDefault;
static AIJob				**g_eaQueuedDestroyJobs = NULL;
//static AICivWanderArea		**s_eaWanderAreas = NULL;
// extern AICivPlayerKillEventManager	*s_killEventManager;

MP_DEFINE(AICivPlayerKillEvent);
MP_DEFINE(AICivPOIUser);

// this may be something that might want to be exposed 
static struct 
{
	F32			fPOICalloutChance;

} s_globalPOIConfig = {0};

// ---------------------------------------------------------------
// Player critter killing events
// ---------------------------------------------------------------

typedef struct AICivPlayerKillEvent
{
	EntityRef	playerEntRef;
	S64			iKillReportTime;
	U32			bCompleted;
} AICivPlayerKillEvent;

typedef struct AICivPlayerKillEventManager
{
	StashTable	stshPlayerKillEvents;
	S64			iLastUpdateTime;
	int			partitionIdx;

	AICivPlayerKillEvent **eaEventsToRemove;

} AICivPlayerKillEventManager;

// ---------------------------------------------------------------
// Civilian Points of Interest
// ---------------------------------------------------------------

// 
typedef struct AICivPOI
{
	GameInteractable			*worldPOI;
	AICivPOIUser				**eaUsers;
	S32							iPartitionIdx;
	//S64							timerLastUsedTime;
} AICivPOI;

typedef struct AICivPOIUser
{
	AICivPOI					*civPoi;
	AICivilianPedestrian		*civ;
	S32							iPOISlot;	// the slot the civ is using on this POI
	bool						bIsUsing;	// set when the civ begins using
} AICivPOIUser;


typedef struct AICivPOIManager
{
	AICivPOI			**eaCivPOI;
	Entity				**eaTmpSearchEnts;
	S64					iLastUpdateTime;
	S32					iCurUpdateIdx;
} AICivPOIManager;


// ---------------------------------------------------------------
// Civilian Wander Area
// ---------------------------------------------------------------

typedef struct AICivWanderArea
{
	Vec3				vWorldMid;
	Vec3				vMtxPos;
	Vec3				vLocalMin, vLocalMax;
	Quat				qInvRot;
	F32					fRadius;
	WorldVolumeEntry	*pVolumeEntry;
	
	
	Beacon				**eaStartBeacons;
	S32					maxWanderers;
	// S32					numCiviliansWandering;
	AICivilianPedestrian	**eaCivilians;
} AICivWanderArea;

//void aiCivWanderArea_AddVolume(AICivilianPartitionState *pPartition, WorldVolumeEntry *pEntry);
//void aiCivWanderArea_RemoveVolume(AICivilianPartitionState *pPartition, WorldVolumeEntry *pEntry);
//void aiCivWanderArea_Shutdown();

static AICivWanderArea* aiCivWanderArea_FindWanderArea(AICivilianPartitionState *pPartition, const Vec3 vPos); 
static Beacon* aiCivWanderArea_GetBestBeacon(AICivWanderArea *pWanderArea, const Vec3 vPos);
static int aiCivWanderArea_IsPosInArea(const AICivWanderArea *pWanderArea, const Vec3 vPos);
static int aiCivWanderArea_IsOtherCivUsingBeacon(const AICivWanderArea *pWanderArea, const AICivilianPedestrian *pedIgnore, const Beacon *pBeacon);

static void aiCivWanderArea_BeginUsing(AICivWanderArea* pWander, AICivilianPedestrian *ped);
static void aiCivWanderArea_StopUsing(AICivWanderArea* pWander, AICivilianPedestrian *ped);


// ---------------------------------------------------------------------------------------
// Function table funcs
static void aiCivPedestrian_Initialize(Entity *e, AICivilianPedestrian *civ, const AICivilianSpawningData *spawnData);
static void aiCivPedestrian_Free(AICivilianPedestrian *civ);
static void aiCivPedestrian_ReachedWaypoint(AICivilianPedestrian *civ, AICivilianWaypoint *curWp);
static F32 aiCivPedestrian_GetDesiredDistFromLeg(const AICivilianPedestrian *civ, const AICivilianWaypoint *relativeWaypt, int forward);
static F32 aiCivPedestrian_InitNextWayPtDistFromLeg(const AICivilianPedestrian *civ, const AICivilianPathLeg *leg, AICivilianWaypoint *pNewWaypoint, int forward);
//void aiCivPedestrian_Tick(Entity *e, AICivilianPedestrian *civ);
static void aiCivPedestrian_ProcessPath(Entity *e, AICivilianPedestrian *civ, const Vec3 pos, const Vec3 vFacingDir);
static bool aiCivPedestrian_ContinuePath(AICivilianPedestrian *civ);

// ---------------------------------------------------------------------------------------
// misc ped funcs
static void aiCivPedestrian_SetAnimList(Entity *e, AICivilianPedestrian *civ, AIAnimList *pAnimList);
static void aiCivPedestrian_SetAnimListByName(Entity *e, AICivilianPedestrian *civ, const char *pszAnimList);
static void aiCivPedestrian_SetAnimBit(Entity *e, AICivilianPedestrian *civ, const char *pszBitName);
static void aiCivPedestrian_StopFSM(Entity *e, AICivilianPedestrian *civ);
static bool isPointInLegColumn(const Vec3 vPos, const AICivilianPathLeg *leg);
static void aiPedestrianTurnAround(AICivilianPedestrian *civ, const Vec3 vCivPos, const Vec3 vRelativeDirection);
static int aiCivPedestrian_IsPathExhausted(AICivilianPedestrian *ped);

// Pathing functions
static int aiCivPedestrian_ReturnToNearestLeg(Entity *e, AICivilianPedestrian *civ);
static int aiCivPedestrian_GotoPosition(Entity *e, AICivilianPedestrian *civ, const Vec3 vPos, F32 targetRot, fpPathingComplete fp, bool bSnapToTarget);
static int aiCivPedestrian_IsGoingToPos(AICivilianPedestrian *civ);
static int aiCivPedestrian_PathToUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos);
static void aiCivPedestrian_ReAddAllWaypoints(AICivilianPedestrian *civ);

// ---------------------------------------------------------------------------------------
// POI
static AICivPOIUser* aiCivPOIUser_Alloc();
static void aiCiv_CleanupPOI(int iPartitionIdx, AICivilianPedestrian *civ);

static void aiCivPOIUser_Free(AICivPOIUser *pAICivPOI);
static int aiCiv_CanUsePOI(AICivilianPedestrian *civ);
static int aiCiv_UsePOI(Entity *e, AICivilianPedestrian *civ, S32 iPOISlot, AICivPOI *pCivPOI);
static int aiCiv_IsUsingPOI(AICivilianPedestrian *ped);
static void aiCivPOI_StopUsing(int iPartitionIdx, AICivPOI *pCivPOI, AICivPOIUser *pUser);
static int aiCivPOI_SetReachedAndGetAction(AICivPOI *pCivPOI, S32 iSlot, FSM **ppFSM, AIAnimList **ppAnimList);
static F32 aiCivPOI_GetAnimDuration(AICivPOI *pCivPOI, S32 iSlot, const Vec3 vPos);
static void aiCivPOI_GetPosRot(AICivPOI *pCivPOI, const AICivPOIUser *pUser, Vec3 vOutPos, F32 *pfOutRot );

static void aiCivPedestrian_DoScare(Entity *e, AICivilianPedestrian *civ, const Vec2 vCivPos);
static void aiCivilianResetPath(Entity *e, AICivilianPedestrian *pCiv, const Vec3 vCurPos, const AICivilianPathLeg *pLeg);


// ---------------------------------------------------------------------------------------
// Pedestrian state handlers
// ECivilianState_WANDER
static void aiCivPedestrian_WanderExit(Entity *e, AICivilianPedestrian *ped);
static void aiCivPedestrian_WanderEnter(Entity *e, AICivilianPedestrian *ped);
static int aiCivPedestrian_WanderUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos);

// ECivilianState_DEFAULT
static void aiCivPedestrian_DefaultExit(Entity *e, AICivilianPedestrian *ped);
static void aiCivPedestrian_DefaultEnter(Entity *e, AICivilianPedestrian *ped);
static int aiCivPedestrian_DefaultUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 vCurPos);

// ECivilianState_SCARED
static void aiCivPedestrian_ScaredExit(Entity *e, AICivilianPedestrian *ped);
static void aiCivPedestrian_ScaredEnter(Entity *e, AICivilianPedestrian *ped);
static int aiCivPedestrian_ScaredUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos);

// ECivilianState_CALLOUT
static void aiCivPedestrian_FSMExit(Entity *e, AICivilianPedestrian *ped);
static void aiCivPedestrian_FSMEnter(Entity *e, AICivilianPedestrian *ped);
static int aiCivPedestrian_FSMUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos);

// ECivilianState_POI
static void aiCivPedestrian_POIExit(Entity *e, AICivilianPedestrian *ped);
static void aiCivPedestrian_POIEnter(Entity *e, AICivilianPedestrian *ped);
static int aiCivPedestrian_POIUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos);

// ---------------------------------------------------------------------------------------
void aiCivPedestrianInitializeData()
{
	s_pedestrianFunctionTable.CivInitialize = (fpCivInitialize) aiCivPedestrian_Initialize;
	s_pedestrianFunctionTable.CivFree = (fpCivFree) aiCivPedestrian_Free;
	s_pedestrianFunctionTable.CivGetDesiredDistFromLeg = (fpCivGetDesiredDistFromLeg) aiCivPedestrian_GetDesiredDistFromLeg;
	s_pedestrianFunctionTable.CivInitNextWayPtDistFromLeg = (fpCivInitNextWayPtDistFromLeg) aiCivPedestrian_InitNextWayPtDistFromLeg;
	s_pedestrianFunctionTable.CivContinuePath = (fpCivContinuePath) aiCivPedestrian_ContinuePath;
	s_pedestrianFunctionTable.CivReachedWaypoint = (fpCivReachedWaypoint) aiCivPedestrian_ReachedWaypoint;
	s_pedestrianFunctionTable.CivProcessPath = (fpCivProcessPath) aiCivPedestrian_ProcessPath;

	
	{
		ZeroStruct(&g_calloutJobDescDefault);
		g_calloutJobDescDefault.fsmName = NULL;
		g_calloutJobDescDefault.jobName = CIV_CALLOUT_JOB_NAME;
	}

}

// ---------------------------------------------------------------------------------------
void aiCivPedestrianOncePerFrame(void)
{
	eaClearEx(&g_eaQueuedDestroyJobs, aiJobDestroy);
}

// ---------------------------------------------------------------------------------------
AICivilianFunctionSet* aiCivPedestrianGetFunctionSet()
{
	return &s_pedestrianFunctionTable;
}

// ---------------------------------------------------------------------------------------
__forceinline static AICivilianPedestrian* getAICivilianAsPed(AICivilian *civ)
{
	return (civ->type == EAICivilianType_PERSON) ? (AICivilianPedestrian*)civ : NULL;
}

// ---------------------------------------------------------------------------------------
__forceinline static const AICivPedestrianScaredParams* aiCivPedestrian_GetScaredParams()
{
	return &g_civSharedState.pMapDef->pedestrian.scaredParams;
}

// ---------------------------------------------------------------------------------------
__forceinline static const AICivPedestrianWanderParams* aiCivPedestrian_GetWanderParams()
{
	return &g_civSharedState.pMapDef->pedestrian.wanderParams;
}

// ---------------------------------------------------------------------------------------
__forceinline static const AICivPedestrianPOIParams* aiCivPedestrian_GetPOIParams()
{
	return &g_civSharedState.pMapDef->pedestrian.POIParams;
}

// ---------------------------------------------------------------------------------------
static const char *aiCivPedestrian_GetRandomChatterMessage(AICivPedestrianDef *civDef)
{
	if (civDef && civDef->pchPedestrianChatterMessage && civDef->pchPedestrianChatterMessage[0])
	{
		return civDef->pchPedestrianChatterMessage;
	}
	else
	{
		return g_civSharedState.pMapDef->pedestrian.pchPedestrianChatterMessage;
	}
}

// ---------------------------------------------------------------------------------------
static const char* aiCivPedestrian_GetScaredMessage( )
{
	return g_civSharedState.pMapDef->pedestrian.scaredParams.pchScaredPedestrianMessage;
}

// ---------------------------------------------------------------------------------------
static void aiCivPedestrian_ExprVarsAdd(ExprContext* context, Entity* scarerEnt)
{
	exprContextSetPointerVarPooledCached(context, targetEntString, scarerEnt, parse_Entity, true, true, &targetEntVarHandle);
}

// ---------------------------------------------------------------------------------------
static void aiCivPedestrian_ExprVarsRemove(ExprContext* context)
{
	exprContextRemoveVarPooled(context, targetEntString);
}

// ---------------------------------------------------------------------------------------
void aiCivPedestrian_FixupPedDef(AICivPedestrianDef *pPedDef) 
{
	if (eaSize(&pPedDef->onClick.eaOnClickInfo) > 0)
		aiCivOnClickDef_Fixup(&pPedDef->onClick);
}

static int _validateExpression(Expression *expr)
{
	int success;
	ExprContext *context = aiGetStaticCheckExprContext();

	aiCivPedestrian_ExprVarsAdd(context, NULL);
	success = exprGenerate(expr, context);	
	aiCivPedestrian_ExprVarsRemove(context);

	return success;
}

// ---------------------------------------------------------------------------------------
int aiCivPedestrian_ValidatePedDef(AICivPedestrianDef *pPedDef, const char *pszFilename) 
{
	if (pPedDef->pchFearBehaviorFSMOverride)
	{
		Referent *p = RefSystem_ReferentFromString(gFSMDict, pPedDef->pchFearBehaviorFSMOverride);
		if (!p)
		{
			ErrorFilenamef(pszFilename, "CivDef (%s) cannot find the FearFSM named (%s).", 
								GET_CIV_DEF_NAME(&AI_CIV_DEFBASE(pPedDef)), pPedDef->pchFearBehaviorFSMOverride);
			return false;
		}
	}

	if (pPedDef->pchAnimListDefault)
	{
		Referent *p = RefSystem_ReferentFromString(g_AnimListDict, pPedDef->pchAnimListDefault);
		if (!p)
		{
			ErrorFilenamef(pszFilename, "CivDef (%s) cannot find the AnimList named (%s).", 
								GET_CIV_DEF_NAME(&AI_CIV_DEFBASE(pPedDef)), pPedDef->pchAnimListDefault);
			return false;
		}
	}

	if (pPedDef->pExprOnScared)
	{
		if (!_validateExpression(pPedDef->pExprOnScared))
			return false;
	}

	if (eaSize(&pPedDef->onClick.eaOnClickInfo) > 0 && 
		!aiCivOnClickDef_Validate(&pPedDef->onClick, pszFilename))
	{
		return false;
	}

	return true;
}

// ---------------------------------------------------------------------------------------
void aiCivPedestrian_FixupTypeDef(AICivPedestrianTypeDef *pPedTypeDef)
{

}


// ---------------------------------------------------------------------------------------
int aiCivPedestrian_ValidateTypeDef(AICivPedestrianTypeDef *pPedTypeDef, const char *pszFilename)
{
	if (pPedTypeDef->scaredParams.pExprOnScared)
	{
		if (!_validateExpression(pPedTypeDef->scaredParams.pExprOnScared))
			return false;
	}

	return true;
}


// ---------------------------------------------------------------------------------------
void aiCivPedestrian_Initialize(Entity *e, AICivilianPedestrian *civ, const AICivilianSpawningData *spawnData)
{
	AICivPedestrianDef *civDef = (AICivPedestrianDef*)AI_CIV_DEF(civ);

	devassert(civDef);

	gslEntityStartReceivingNearbyPlayerFlag(e);

	mmAICivilianMovementSetDesiredSpeed(CIV_BASE(civ).requester, 
										AI_CIV_DEFBASE(civDef).fSpeedMin, 
										AI_CIV_DEFBASE(civDef).fSpeedRange);

	civ->distFromLeg = randomF32() * ((spawnData->leg->width * 0.5f) - s_fPedestrianColRadius);

	// set the default animList if there is one
	if (civDef->pchAnimListDefault)
	{
		aiCivPedestrian_SetAnimListByName(e, civ, civDef->pchAnimListDefault);
	}
}

// ---------------------------------------------------------------------------------------
void aiCivPedestrian_Free(AICivilianPedestrian *civ)
{
	Entity *e = entFromEntityRefAnyPartition(civ->civBase.myEntRef);
	int iPartitionIdx = CIV_BASE(civ).iPartitionIdx;
	
	if (civ->animListCmdQueue)
	{
		CommandQueue_Destroy(civ->animListCmdQueue);
	}

	if (civ->pPOIUser)
	{
		aiCivPOI_StopUsing(iPartitionIdx, civ->pPOIUser->civPoi, civ->pPOIUser);

		aiCivPOIUser_Free(civ->pPOIUser);
		civ->pPOIUser = NULL;
	}
	
	if(e && aiCivilian_IsDoingCivProcessingOnly(e, CIV_BASEPTR(civ)))
	{
		AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
		devassert(pPartition);
		pPartition->iNormalCivilianCount--;
	}
	
	if (civ->pWanderArea)
	{
		aiCivWanderArea_StopUsing(civ->pWanderArea, civ);
	}
	
}

void aiCivPedestrian_InitWaypointDefault(AICivilianPedestrian *civ, AICivilianWaypoint *wp)
{
	wp->distFromLeg = civ->distFromLeg;
}


// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_UpdateVolumeCache(Entity *e, const Vec2 vPos)
{
	static U32 volumeTypes = 0;
	if (!volumeTypes)
	{
		volumeTypes = wlVolumeTypeNameToBitMask("InteractionAttached");
		if (!volumeTypes)
			return;
	}

	// NOTE: when ENTITYFLAG_CIV_PROCESSING_ONLY is not set, they will do the normal
	// volume cache query stuff that every entity does, not sure if it is bad, 
	// but it's been that way since it was first set up
	
	// currently we're only using this for gates, so maybe we can pre-cache if there are even any gates on the map
	if (entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY))
	{	// use a sphere because that's the cheapest query type there seems to be
		wlVolumeCacheQuerySphereByType(e->volumeCache, vPos, 3.f, volumeTypes);
	}
}


// ---------------------------------------------------------------------------------------
void aiCivPedestrian_Tick(Entity *e, AICivilianPedestrian *civ)
{
	Vec3 vPos; 
	Vec3 vFacingDir;
	int partitionIdx = entGetPartitionIdx(e);

	PERFINFO_AUTO_START("ped", 1);

	if (civ->bScareReceived)
	{
		entGetPos(e, vPos);
		aiCivPedestrian_DoScare(e, civ, vPos);
		PERFINFO_AUTO_STOP();
		return;
	}
	
	// only invalidate the cached position if the civilian is crossing a crosswalk
	if(civ->bIsUsingCrosswalk)
	{
		civ->civBase.v2PosIsCurrent = false;
	}

	if(ABS_TIME_SINCE_PARTITION(partitionIdx, civ->civBase.lastUpdateTime) < SEC_TO_ABS_TIME(s_fPed_UpdatePerSeconds))
	{
		PERFINFO_AUTO_STOP();
		return;
	}

	aiThinkTickDebug(e, e->aibase, ABS_TIME_PARTITION(partitionIdx));

	// 
	entGetPos(e, vPos);
	if(interactable_HasVolumeTriggeredGates())
		aiCivPedestrian_UpdateVolumeCache(e, vPos);

	civ->civBase.lastUpdateTime = ABS_TIME_PARTITION(partitionIdx);

	if(FALSE_THEN_SET(civ->civBase.v2PosIsCurrent))
	{
		civCopyVec3ToVec2(vPos, civ->civBase.v2Pos);
	}

	setVec3(vFacingDir, 0.0f, 0.0f, 1.0f);
	
	
	if (CIV_BASE(civ).bIsKnocked)
	{
		if (e->mm.mrSurface && !pmKnockIsActive(e))
		{
			CIV_BASE(civ).bIsKnocked = false;
			// the knock is no longer active, tell the default requester to be disabled
			// and re-enable the civilian movement requester
			mrSurfaceSetEnabled(e->mm.mrSurface, false);
			mmAICivilianMovementEnable(CIV_BASE(civ).requester, true);
			// reacquire the leg
			aiCivPedestrian_ReturnToNearestLeg(e, civ);
			CIV_BASE(civ).timeatLastDefaultRequester = ABS_TIME_PARTITION(partitionIdx);
		}
	}
	else if (e->mm.mrSurface && entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY))
	{
		// if we have a surface requester and are only doing civilian processing, 
		// destroy it if it's been more than X seconds since we were last using it
		if(ABS_TIME_SINCE_PARTITION(partitionIdx, CIV_BASE(civ).timeatLastDefaultRequester) > SEC_TO_ABS_TIME(s_fPed_SurfaceRequesterDelTime))
		{
			gslEntMovementDestroySurfaceRequester(e);
		}
	}
	
	aiCivilianProcessPath(e, &civ->civBase, vPos, vFacingDir);

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
F32 aiCivPedestrian_GetDesiredDistFromLeg(const AICivilianPedestrian *civ, const AICivilianWaypoint *relativeWaypt, int forward)
{
	F32 mult = forward ? 1 : -1;
	F32 bounding_offset = s_fPedestrianColRadius;

	if (relativeWaypt->distFromLeg > 0) bounding_offset = -bounding_offset;

	return (relativeWaypt->distFromLeg + bounding_offset + relativeWaypt->medianDist * 0.5f) * mult;
}

// ------------------------------------------------------------------------------------------------------------------
F32 aiCivPedestrian_InitNextWayPtDistFromLeg(	const AICivilianPedestrian *civ, 
												const AICivilianPathLeg *leg, 
												AICivilianWaypoint *pNewWaypoint, 
												int forward)
{
	const AICivilianWaypoint *pLastWaypoint = aiCivilianGetFromLastWaypoint(&civ->civBase, 0);
	F32 mult = forward ? 1 : -1;
	F32 legHalfWidth;
	
	if (leg->width > 3.f) legHalfWidth = (leg->width - 2.5f) * 0.5f;
	else legHalfWidth = 1.f;

	devassert(pLastWaypoint);

	pNewWaypoint->medianDist = leg->median_width;
	
	if (pLastWaypoint->bIsLeg)
	{
		#define MAX_NORMALIZED_DEVIATION (0.25f)
		F32 normalizedDist = pLastWaypoint->distFromLeg / (pLastWaypoint->leg->width * 0.5f);
		
		normalizedDist += randomF32() * MAX_NORMALIZED_DEVIATION;
		normalizedDist = CLAMP(normalizedDist, -1.0f, 1.0f);

		pNewWaypoint->distFromLeg = normalizedDist * legHalfWidth;
	}
	else
	{
		// last waypoint was an intersection.
		// so, just use its distFromLeg and clamp it to the leg's width
		pNewWaypoint->distFromLeg = CLAMP(pLastWaypoint->distFromLeg, -legHalfWidth, legHalfWidth);
	}

	return (pNewWaypoint->distFromLeg + pNewWaypoint->medianDist*0.5f) * mult;
}

static __forceinline void aiCivDebug_PushLastState(AICivilianPedestrian *civ, ECivilianState eLastState)
{
	U32 i;
		
	for (i = CIVPREVIOUS_HISTORY_SIZE-1; i > 0; --i)
	{
		civ->previousStates[i] = civ->previousStates[i - 1];
	}
	civ->previousStates[0] = eLastState;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_SetState(AICivilianPedestrian *civ, ECivilianState eNewState)
{
	ECivilianState oldState;
	Entity *ent;

	if (civ->eState == eNewState)
		return;

	ent = entFromEntityRefAnyPartition(civ->civBase.myEntRef);

	oldState = civ->eState;
	
	aiCivDebug_PushLastState(civ, civ->eState);

	civ->eState = eNewState;
	civ->timeLastStateChange = ABS_TIME_PARTITION(civ->civBase.iPartitionIdx);

	// leaving the current state, do some clean-up
	switch(oldState)
	{
		acase ECivilianState_DEFAULT:
		{
			aiCivPedestrian_DefaultExit(ent, civ);
		}
		xcase ECivilianState_SCARED:
		{
			aiCivPedestrian_ScaredExit(ent, civ);
		}
		xcase ECivilianState_FSM:
		{
			aiCivPedestrian_FSMExit(ent, civ);
		}
		xcase ECivilianState_POI:
		{
			aiCivPedestrian_POIExit(ent, civ);
		}
		xcase ECivilianState_WANDER:
		{
			aiCivPedestrian_WanderExit(ent, civ);
		}
	}
	
	switch(civ->eState)
	{
		acase ECivilianState_DEFAULT:
		{
			aiCivPedestrian_DefaultEnter(ent, civ);
		}
		xcase ECivilianState_SCARED:
		{
			aiCivPedestrian_ScaredEnter(ent, civ);
		}
		xcase ECivilianState_FSM:
		{
			aiCivPedestrian_FSMEnter(ent, civ);
		}
		xcase ECivilianState_POI:
			aiCivPedestrian_POIEnter(ent, civ);

		xcase ECivilianState_WANDER:
			aiCivPedestrian_WanderEnter(ent, civ);
	}
}






// ------------------------------------------------------------------------------------------------------------------
// Tells the civilian to run the FSM
static int aiCivilianRunFSM(Entity *e, AICivilianPedestrian *civ, FSM *pFSM)
{
	AIVarsBase *aib;
	if(!e || !civ || !pFSM)
		return false;

	aib = e->aibase;
	/*
	if(!entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY))
	return;  // Already running an FSM
	*/

	aib->doBScript = 1;

	aib->skipOnEntry = false;
	if(aib->fsmContext)
		fsmContextDestroy(aib->fsmContext);

	aib->fsmContext = fsmContextCreate(pFSM);
	if(!aib->fsmMessages)
		aib->fsmMessages = stashTableCreateWithStringKeys(4, StashDefault);
	aib->fsmContext->messages = aib->fsmMessages;
	entGetPos(e, civ->calloutStartPos);

	exprContextSetFSMContext(aib->exprContext, aib->fsmContext);

	aiCivPedestrian_SetState(civ, ECivilianState_FSM);
	return true;
}


// ------------------------------------------------------------------------------------------------------------------
static int aiCivilianRunFSMByName(Entity *e, AICivilianPedestrian *civ, const char *fsmName)
{
	FSM *pFSM = (FSM*)RefSystem_ReferentFromString(gFSMDict, fsmName);

	return aiCivilianRunFSM(e, civ, pFSM);
}


// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_StopFSMInternal(Entity *e, AICivilianPedestrian *civ)
{
	if(e->aibase->fsmContext)
		fsmExitCurState(e->aibase->fsmContext);
	
	aiCivPedestrian_StopFSM(e, civ);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_StopFSM(Entity *e, AICivilianPedestrian *civ)
{
	Vec3 pos;
	entGetPos(e, pos);

	e->aibase->doBScript = 0;
	aiCivPedestrian_SetState(civ, ECivilianState_DEFAULT);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_SetSpeedBasedOnState(Entity *e, AICivilianPedestrian *civ)
{
	if (civ->civBase.requester)
	{
		if (civ->eState != ECivilianState_SCARED)
		{
			if (civ->bIsUsingCrosswalk && (AI_CIV_DEF(civ)->fSpeedMin < s_fCivPedestrianMinSpeedCrosswalk))
			{	// we want civilians crossing the street to not dilly-dally
				// if we don't move fast enough for the crosswalk, set the crosswalk speed
				AICivilianWaypoint *curWp = aiCivilianGetCurrentWaypoint(&civ->civBase);
				F32 fMinSpeed, fSpeedRange;

				if (curWp && ! aiCivCrosswalk_IsCrosswalkOpen(curWp->pCrosswalkUser))
				{	// light is changing, start running
					fMinSpeed = 10.6f;
					fSpeedRange = 4.f;
				}
				else
				{
					fMinSpeed = s_fCivPedestrianMinSpeedCrosswalk;
					fSpeedRange = s_fCivPedestrianSpeedRangeCrosswalk;
				}

				mmAICivilianMovementSetDesiredSpeed(civ->civBase.requester, fMinSpeed, fSpeedRange);
			}
			else 
			{
				mmAICivilianMovementSetDesiredSpeed(civ->civBase.requester, 
					AI_CIV_DEF(civ)->fSpeedMin, 
					AI_CIV_DEF(civ)->fSpeedRange);
			}
		}
		else
		{
			const AICivPedestrianScaredParams* pScaredParams = aiCivPedestrian_GetScaredParams();

			mmAICivilianMovementSetDesiredSpeed(civ->civBase.requester, 
												pScaredParams->fMinimumSpeed, 
												pScaredParams->fSpeedRange);
		}	
	}
	else
	{
		if (civ->eState == ECivilianState_SCARED)
		{
			const AICivPedestrianScaredParams* pScaredParams = aiCivPedestrian_GetScaredParams();
			F32 fSpeed = pScaredParams->fMinimumSpeed + randomPositiveF32() * pScaredParams->fSpeedRange;
			aiMovementSetOverrideSpeed(e, e->aibase, fSpeed);
		}
		else
		{
			aiMovementSetWalkRunDist(e, e->aibase, 1.f, 80.f, false);
		}
	}
	
}

void aiCivPedestrian_ReachedWaypoint(AICivilianPedestrian *civ, AICivilianWaypoint *curWp)
{
	civ->distFromLeg = curWp->distFromLeg;

	if (curWp->pCrosswalkUser)
	{
		aiCivCrosswalk_ReleaseUser(&curWp->pCrosswalkUser);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivPedestrian_ReacquireLeg(Entity *e, AICivilianPedestrian *civ, const Vec3 vPos)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e)); 
	AICivilianPathLeg *leg = aiCivilian_GetLegByPos(pPartition->pWorldLegGrid, civ->civBase.type, vPos, NULL);
	if (!leg)
	{
		civ->civBase.flaggedForKill = 1;
		return;
	}
	aiCivilianResetPath(e, civ, vPos, leg);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivPedestrian_ProcessPath(Entity *e, AICivilianPedestrian *civ, const Vec3 pos, const Vec3 vFacingDir)
{
	AICivilianWaypoint *curWp = NULL;

	if(entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY))
	{
		if (sameVec3(civ->civBase.prevPos, pos))
		{
			// a fail-safe to get civilians to not get stuck on pathing across crosswalks
			civ->ticksNotMoving++;
			if (civ->ticksNotMoving > 100)
			{
				aiCivPedestrian_ReacquireLeg(e, civ, pos);
				civ->ticksNotMoving = 0;
			}
		}
		else
		{
			civ->ticksNotMoving = 0;
		}

		curWp = aiCivilianGetCurrentWaypoint(&civ->civBase);
		// deal with specific waypoint stuff, crosswalks in particular
		if (curWp)
		{
			if (curWp->pCrosswalkUser)
			{
				if (curWp->bStop)
				{
					aiCivCrosswalk_SetUserWaiting(curWp->pCrosswalkUser);

					// only reason peds can be stopped is because of crosswalks
					if (aiCivCrosswalk_CanCivCross(curWp->leg))
					{
						if (civ->bIsUsingCrosswalk == false)
						{
							civ->bIsUsingCrosswalk = true;
							civ->civBase.v2PosIsCurrent = false;
							devassert(curWp->pCrosswalkUser);
							aiCivCrosswalk_SetUserCrossingStreet(curWp->pCrosswalkUser);
							aiCivPedestrian_SetSpeedBasedOnState(e, civ);
						}

						curWp->bStop = false;
					}
				}
				else
				{	// as we are crossing the crosswalk, keep updating our speed because
					// the light might have changed
					aiCivPedestrian_SetSpeedBasedOnState(e, civ);
				}
				
			}
			else if (! curWp->bIsCrosswalk && civ->bIsUsingCrosswalk)
			{
				civ->bIsUsingCrosswalk = false;
				aiCivPedestrian_SetSpeedBasedOnState(e, civ);
			}
			else if (curWp->bStop)
			{
				curWp->bStop = false;
			}
		}
	}
	
	// update the current state
	switch (civ->eState)
	{
		acase ECivilianState_SCARED:
		{
			if (aiCivPedestrian_ScaredUpdate(e, civ, pos))
				return;
		}

		xcase ECivilianState_DEFAULT:
		{
			if (aiCivPedestrian_DefaultUpdate(e, civ, pos))
				return;
		}

		xcase ECivilianState_FSM:
		{
			// Running callout or otherwise indisposed, 
			if (aiCivPedestrian_FSMUpdate(e, civ, pos))
				return;
		}

		xcase ECivilianState_POI:
		{
			if (aiCivPedestrian_POIUpdate(e, civ, pos))
				return;
		}

		xcase ECivilianState_WANDER:
		{
			if (aiCivPedestrian_WanderUpdate(e, civ, pos))
				return;

		}
	}

	if (!aiCivPedestrian_IsGoingToPos(civ))
	{
		if(entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY))
		{
			if (civ->civBase.spawnVolume)
			{
				// if we have a spawn volume, lets check our path
				// if the path leaves the spawn volume, then we're going to turn around.
				if (! pointBoxCollision(pos, civ->civBase.spawnVolume->min, civ->civBase.spawnVolume->max))
				{
					// we are currently inside the volume, get a future path point
					if (pointBoxCollision(civ->civBase.prevPos, civ->civBase.spawnVolume->min, civ->civBase.spawnVolume->max))
					{
						Vec3 vRelativeDirection;

						subVec3(pos, civ->civBase.prevPos, vRelativeDirection);
						// our path ends outside of the area.
						// (for now, just immediately turn around.
						aiPedestrianTurnAround(civ, pos, vRelativeDirection);
					}
				}
				else
				{
					// we are no longer in our designated spawn area.
					// TODO: add bias to continue path so civilians have a chance to get near to where they're from
					// otherwise when the clumping issue is solved, we can first delete civilians who are not where they
					// are supposed to be!
				}
			}

			if (e->nearbyPlayer != civ->bPlayerIsNearby)
			{
				civ->bPlayerIsNearby = e->nearbyPlayer;
				mmAICivilianMovementSetCollision(civ->civBase.requester, civ->bPlayerIsNearby);
			}

			aiCivilianCheckReachedWpAndContinue(e, &civ->civBase, pos);
		}
	}
	else 
	{	
		// doing normal aiMovement pathing and doing an pedestrian goto behavior
		aiCivPedestrian_PathToUpdate(e, civ, pos);
	}

}

// -------------------------------------------------------------------------------------------------------
// ECivilianState_DEFAULT
// -------------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_DefaultEnter(Entity *e, AICivilianPedestrian *ped)
{
	AICivPedestrianDef *civDef = (AICivPedestrianDef*)AI_CIV_DEF(ped);
	
	// if we don't have a path or were told to go to some arbitrary position,
	// return to the nearest leg
	if (!aiCivPedestrian_IsGoingToPos(ped))
	{
		aiCivPedestrian_ReturnToNearestLeg(e, ped);
	}


	if (CIV_BASE(ped).requester)
	{
		mmAICivilianMovementSetDesiredSpeed(CIV_BASE(ped).requester, 
											AI_CIV_DEFBASE(civDef).fSpeedMin, 
											AI_CIV_DEFBASE(civDef).fSpeedRange);
	}

	if (civDef->pchAnimListDefault)
	{
		aiCivPedestrian_SetAnimListByName(e, ped, civDef->pchAnimListDefault);
	}
}

// -------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_DefaultExit(Entity *e, AICivilianPedestrian *ped)
{
	if (ped->animListCmdQueue)
	{
		aiCivPedestrian_SetAnimList(e, ped, NULL);
	}

	aiCivilianPauseMovement(CIV_BASEPTR(ped), false);
}

// -------------------------------------------------------------------------------------------------------
static int aiCivPedestrian_DefaultUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 vCurPos)
{
	if (! aiCivPedestrian_IsGoingToPos(ped))
	{	
		// we're doing normal civilian movement, 
		// otherwise we're still a normal critter pathing back to our closest leg
		if(!aiCivilian_IsDoingCivProcessingOnly(e, CIV_BASEPTR(ped)))
		{
			aiCivilianBeCheap(e, CIV_BASEPTR(ped));
			aiCivPedestrian_ReAddAllWaypoints(ped);
		}

		if (ped->idleTime != 0)
		{
			if (timeSecondsSince2000() > ped->idleTime)
			{
				aiCivilianPauseMovement(CIV_BASEPTR(ped), false);
				ped->idleTime = 0;
			}
		}
		else if (!ped->bIsUsingCrosswalk && randomChance(s_fCivIdleChance))
		{
			aiCivilianPauseMovement(CIV_BASEPTR(ped), true);
			ped->idleTime = timeSecondsSince2000() + 4 + (U32)(randomPositiveF32() * 6.0f);
		}
		else if (eaSize(&CIV_BASEPTR(ped)->path.eaWaypoints) == 0) 
		{	// if we don't have a path, reacquire one
			aiCivPedestrian_ReacquireLeg(e, ped, vCurPos);
		}


		if (s_iCivDoWandering && randomChance(0.05f))
		{
			AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e));
			AICivWanderArea *pWanderArea = aiCivWanderArea_FindWanderArea(pPartition, vCurPos);
			if (pWanderArea)
			{
				ped->pWanderArea = pWanderArea;
				aiCivPedestrian_SetState(ped, ECivilianState_WANDER);
				return true;
			}
		}

		if (s_iCivDoChatter && randomChance(s_fCivChatterChance))
		{
			AICivPedestrianDef *civDef = (AICivPedestrianDef*)AI_CIV_DEF(ped);
			const char *chatterMsg = aiCivPedestrian_GetRandomChatterMessage(civDef);
			if (chatterMsg)
			{
				aiSayMessageInternal(e, NULL, NULL, chatterMsg, NULL, s_fCivMessageDuration);
			}
		}
	}
	

	return false;
}


// -------------------------------------------------------------------------------------------------------
// ECivilianState_SCARED
// -------------------------------------------------------------------------------------------------------


// -------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_ScaredEnter(Entity *e, AICivilianPedestrian *ped)
{
	const AICivPedestrianScaredParams* pScaredParams = aiCivPedestrian_GetScaredParams();

	if (aiCivPedestrian_IsGoingToPos(ped) || aiCivPedestrian_IsPathExhausted(ped))
	{
		aiCivPedestrian_ReturnToNearestLeg(e, ped);
	}

	if (CIV_BASE(ped).requester)
	{
		if (!pScaredParams->bUseCritterSpeed)
		{
			mmAICivilianMovementSetDesiredSpeed(CIV_BASE(ped).requester, 
												pScaredParams->fMinimumSpeed, 
												pScaredParams->fSpeedRange);
		}
		else
		{
			mmAICivilianMovementUseCritterOverrideSpeed(CIV_BASE(ped).requester, true);
		}
	}
	
		
	aiCivPedestrian_SetAnimBit(e, ped, CIV_SCARED_BIT_NAME);
}

// -------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_ScaredExit(Entity *e, AICivilianPedestrian *ped)
{
	const AICivPedestrianScaredParams* pScaredParams = aiCivPedestrian_GetScaredParams();

	aiCivilianPauseMovement(CIV_BASEPTR(ped), false);
	aiCivPedestrian_SetAnimList(e, ped, NULL);

	if (pScaredParams->bUseCritterSpeed && CIV_BASE(ped).requester)
	{
		mmAICivilianMovementUseCritterOverrideSpeed(CIV_BASE(ped).requester, false);
	}
}

// -------------------------------------------------------------------------------------------------------
static int aiCivPedestrian_ScaredUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos) 
{
	int partitionIdx = entGetPartitionIdx(e);
	if (! aiCivPedestrian_IsGoingToPos(ped))
	{
		const AICivPedestrianScaredParams* pScaredParams = aiCivPedestrian_GetScaredParams();
				
		// if we are cowering in fear
		if (ped->civBase.bPaused)
		{
			if (ABS_TIME_SINCE_PARTITION(partitionIdx, ped->timeLastStateChange) > SEC_TO_ABS_TIME(pScaredParams->fCowerTime))
			{
				aiCivilianPauseMovement(CIV_BASEPTR(ped), false);
			}
		}

		if (ABS_TIME_SINCE_PARTITION(partitionIdx, ped->timeLastStateChange) > SEC_TO_ABS_TIME(pScaredParams->fScaredTime))
		{
			aiCivPedestrian_SetState(ped, ECivilianState_DEFAULT);
		}
	}

	return false;
}

// -------------------------------------------------------------------------------------------------------
// ECivilianState_FSM
// -------------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------------
void aiCivPedestrian_FSMEnter(Entity *e, AICivilianPedestrian *ped)
{
	// become a normal critter if we aren't already
	aiCivilianBeNormal(e, &ped->civBase);
}

// -------------------------------------------------------------------------------------------------------
void aiCivPedestrian_FSMExit(Entity *e, AICivilianPedestrian *ped)
{
	if (ped->bPOIFSM)
	{
		ped->lastPOITime = timeSecondsSince2000();
		aiCiv_CleanupPOI(entGetPartitionIdx(e), ped);

		ped->bPOIFSM = false;
	}
}


// -------------------------------------------------------------------------------------------------------
int aiCivPedestrian_FSMUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos)
{	
	return true;
}

// -------------------------------------------------------------------------------------------------------
// ECivilianState_POI
// -------------------------------------------------------------------------------------------------------

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_PathToPOIComplete(Entity *e, AICivilianPedestrian *civ, 
											  const Vec3 vCurPos, int bPathTimedOut)
{
	int partitionIdx = entGetPartitionIdx(e);
	if (! civ->pPOIUser || bPathTimedOut)
	{
		aiCiv_CleanupPOI(entGetPartitionIdx(e),civ);
		aiCivPedestrian_SetState(civ, ECivilianState_DEFAULT);
		return;
	}

	// get the action from the POI
	{
		AICivPOI *pCivPOI = civ->pPOIUser->civPoi;
		FSM *pFSM = NULL;
		AIAnimList *pAnimList = NULL;

		civ->pPOIUser->bIsUsing = true;
		
		if (aiCivPOI_SetReachedAndGetAction(pCivPOI, civ->pPOIUser->iPOISlot, &pFSM, &pAnimList))
		{
			if (pFSM)
			{
				civ->bPOIFSM = true;
				// will change to FSM state
				aiCivilianRunFSM(e, civ, pFSM);
			}
			else if (pAnimList)
			{
				// reset our state timer to time the we want to be in this animation
				civ->timeLastStateChange = ABS_TIME_PARTITION(partitionIdx);
				civ->fAmbientJobTime = aiCivPOI_GetAnimDuration(pCivPOI, civ->pPOIUser->iPOISlot, vCurPos);
				aiCivPedestrian_SetAnimList(e, civ, pAnimList);
			}
		}
		else
		{	// failed to get an appropriate action for the POI
			
			aiCivPedestrian_SetState(civ, ECivilianState_DEFAULT);
		}
	}

}


// -------------------------------------------------------------------------------------------------------
void aiCivPedestrian_POIEnter(Entity *e, AICivilianPedestrian *ped)
{
	Vec3 vTargetPos;
	F32 fTargetRot = 0.f;

	AICivPOI *pCivPOI;

	if (!ped->pPOIUser)
	{
		return;
	}

	pCivPOI = ped->pPOIUser->civPoi;

	aiCivPOI_GetPosRot(pCivPOI, ped->pPOIUser, vTargetPos, &fTargetRot);
	
	// go to the POI position
	ped->bFailedPath = !aiCivPedestrian_GotoPosition(e, ped, vTargetPos, fTargetRot, aiCivPedestrian_PathToPOIComplete, true);

	ped->lastPOITime = timeSecondsSince2000();
}

// -------------------------------------------------------------------------------------------------------
void aiCivPedestrian_POIExit(Entity *e, AICivilianPedestrian *ped)
{
	if (!ped->bPOIFSM)
	{	// if we weren't using an FSM
		aiCiv_CleanupPOI(entGetPartitionIdx(e),ped);
		aiCivPedestrian_SetAnimList(e, ped, NULL);
		ped->lastPOITime = timeSecondsSince2000();

		aiCivilian_ClearWaypointList(CIV_BASEPTR(ped));
	}
	
}


// -------------------------------------------------------------------------------------------------------
int aiCivPedestrian_POIUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos)
{
	int partitionIdx = entGetPartitionIdx(e);
	if (ped->bFailedPath)
	{
		aiCivPedestrian_SetState(ped, ECivilianState_DEFAULT);
		return true;
	}

	if (! aiCivPedestrian_IsGoingToPos(ped))
	{
		if (ABS_TIME_SINCE_PARTITION(partitionIdx, ped->timeLastStateChange) > SEC_TO_ABS_TIME(ped->fAmbientJobTime))
		{
			aiCivPedestrian_SetState(ped, ECivilianState_DEFAULT);
		}	
		return true;
	}
	
	return false;
}

// -------------------------------------------------------------------------------------------------------
// ECivilianState_WANDER
// -------------------------------------------------------------------------------------------------------

// -------------------------------------------------------------------------------------------------------
void aiCivPedestrian_PathToWanderComplete(Entity *e, AICivilianPedestrian *ped, const Vec3 vCurPos, int bPathTimedOut)
{
	int partitionIdx = entGetPartitionIdx(e);
	if (bPathTimedOut)
	{
		if (ped->pWanderArea)
			aiCivWanderArea_StopUsing(ped->pWanderArea, ped);
		aiCivPedestrian_SetState(ped, ECivilianState_DEFAULT);
		return;
	}

	aiCivilianBeCheap(e, CIV_BASEPTR(ped));
	aiCivPedestrian_SetSpeedBasedOnState(e, ped);

	{
		const AICivPedestrianWanderParams* pWanderParams = aiCivPedestrian_GetWanderParams();
		ped->fAmbientJobTime = pWanderParams->fWanderTimeMin + 
			(randomPositiveF32() * pWanderParams->fWanderTimeRange);
	}

	ped->lastWanderTime = ABS_TIME_PARTITION(partitionIdx);
}

// -------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_WanderEnter(Entity *e, AICivilianPedestrian *ped)
{
	Vec3 vPos;
	
	aiCivilian_ClearWaypointList(CIV_BASEPTR(ped));

	entGetPos(e, vPos);
	
	ped->pPrevBeacon = NULL;
	ped->pBeacon = aiCivWanderArea_GetBestBeacon(ped->pWanderArea, vPos);
	if (!ped->pBeacon)
		return;

	aiCivWanderArea_BeginUsing(ped->pWanderArea, ped);
	ped->bFailedPath = !aiCivPedestrian_GotoPosition(e, ped, ped->pBeacon->pos, 0.f, aiCivPedestrian_PathToWanderComplete, false);

}

// -------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_WanderExit(Entity *e, AICivilianPedestrian *ped)
{
	if (ped->pWanderArea)
	{
		aiCivWanderArea_StopUsing(ped->pWanderArea, ped);
	}

	aiCivilian_ClearWaypointList(CIV_BASEPTR(ped));
}

// -------------------------------------------------------------------------------------------------------
static int aiCivPedestrian_WanderUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos)
{
	int partitionIdx = entGetPartitionIdx(e);
	if (aiCivPedestrian_IsGoingToPos(ped))
		return false;

	if (ped->bFailedPath)
	{
		aiCivPedestrian_SetState(ped, ECivilianState_DEFAULT);
		return true;
	}
	
	if (!ped->pWanderArea || !ped->pBeacon)
	{
		aiCivPedestrian_SetState(ped, ECivilianState_DEFAULT);
		return true;
	}
	
	if (ABS_TIME_SINCE_PARTITION(partitionIdx, ped->timeLastStateChange) > SEC_TO_ABS_TIME(ped->fAmbientJobTime))
	{
		aiCivPedestrian_SetState(ped, ECivilianState_DEFAULT);
		return true;
	}
		
	if (ped->lastWanderTime == 0)
	{
		AICivilianPath *path;
		S32 numReached;
		
		numReached = mmAICivilianGetAndClearReachedWp(CIV_BASE(ped).requester);
		
		path = &CIV_BASE(ped).path;

		//if (numReached > 0)
		{
			path->curWp += numReached;
			if (path->curWp >= eaSize(&path->eaWaypoints))
			{
				const AICivPedestrianWanderParams* pWanderParams = aiCivPedestrian_GetWanderParams();

				F32 fWaitTime = pWanderParams->fTimeAtPointAvg + 
									(randomF32() * pWanderParams->fTimeAtPointAvg * 0.5f);
				ped->lastWanderTime = ABS_TIME_PARTITION(partitionIdx) + SEC_TO_ABS_TIME(fWaitTime);
			}
		}
	}
	else
	{
		if (ABS_TIME_PARTITION(partitionIdx) > ped->lastWanderTime)
		{
			// time to find a new beacon to path to
			S32 i;
			Beacon *pBestBcn = NULL;
			F32 fBestHeuristic = -1.f;
			Vec3 vCurPedDir;

			aiCivilianPruneOldWaypoints(CIV_BASEPTR(ped));

			if (ped->pPrevBeacon)
			{
				subVec3(ped->pBeacon->pos, ped->pPrevBeacon->pos, vCurPedDir);
			}
			else
			{
				Vec2 pyFace; 
				entGetFacePY(e, pyFace);
				setVec3FromYaw(vCurPedDir, pyFace[1]);
			}

						
			ped->lastWanderTime = 0;
			// go through all the ground connections of the beacon
			for(i = 0; i < ped->pBeacon->gbConns.size; i++)
			{
				BeaconConnection *pConn =  ped->pBeacon->gbConns.storage[i];
				Beacon* pConnBeacon = pConn->destBeacon;
				
				if (aiCivWanderArea_IsPosInArea(ped->pWanderArea, pConnBeacon->pos) && 
					!aiCivWanderArea_IsOtherCivUsingBeacon(ped->pWanderArea, ped, pConnBeacon))
				{
					Vec3 vBeaconConnectDir;
					F32 fCurHeuristic;

					subVec3(pConnBeacon->pos, ped->pBeacon->pos, vBeaconConnectDir);
					fCurHeuristic = dotVec3(vBeaconConnectDir, vCurPedDir) > 0 ?
										0.5f + randomPositiveF32() * 5.f : randomPositiveF32() * 1.f;
					
					if (fBestHeuristic < fCurHeuristic)
					{
						pBestBcn = pConnBeacon;
						fBestHeuristic = fCurHeuristic;
					}
				}
			}

			if (pBestBcn)
			{
				AICivilianWaypoint *pWayPoint = aiCivilianWaypointAlloc();
				
				ped->pPrevBeacon = ped->pBeacon;
				ped->pBeacon = pBestBcn;

				copyVec3(ped->pBeacon->pos, pWayPoint->pos);
				aiCiv_AddNewWaypointToPath(&ped->civBase.path, pWayPoint);
				aiCiv_SendQueuedWaypoints(CIV_BASEPTR(ped));
			}
			else
			{
				ped->pBeacon = NULL;
				return true;
			}
		}
	}

	
	return true;
}



// ------------------------------------------------------------------------------------------------------------------
// checks if the normal civilian path is exhausted
static int aiCivPedestrian_IsPathExhausted(AICivilianPedestrian *ped)
{
	return ped->civBase.path.curWp >= eaSize(&ped->civBase.path.eaWaypoints);
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianWaypoint* aiCivilianClearPath_KeepCurWp(AICivilian *pCiv)
{
	AICivilianWaypoint *pCurWp;

	pCurWp = aiCivilianGetCurrentWaypoint(pCiv);
	if (pCurWp->pCrosswalkUser)
	{
		aiCivCrosswalk_ReleaseUser(&pCurWp->pCrosswalkUser);
	}
	pCurWp->bReverse = false;
	pCurWp->bIsCrosswalk = false;
	pCurWp->bStop = false;

	// remove it from the list so we don't destroy it when we clear the list
	eaRemoveFast(&pCiv->path.eaWaypoints, pCiv->path.curWp);

	aiCivilian_ClearWaypointList(pCiv);

	// put our current waypoint back on the list
	aiCiv_AddNewWaypointToPath(&pCiv->path, pCurWp);
	return pCurWp;
}

// ------------------------------------------------------------------------------------------------------------------
// finds the closest leg 
static AICivilianPathLeg* aiCivFindClosestLegInIntersection(const AICivilianPathIntersection *pAcpi, 
															S32 iForward, 
															const Vec3 vCivPos, 
															const Vec3 vRelativeDirection )
{
	Vec3 vToLeg;
	AICivilianPathLeg *pBestLeg = NULL;
	F32 fDot;

	// find the closest leg in the intersection in opposite direction
	if (!pAcpi->bIsMidIntersection)
	{
		S32 i;
		F32 fBestDistSQ = FLT_MAX;
		for (i = 0; i < eaSize(&pAcpi->legIntersects); i++)
		{
			PathLegIntersect *pli = pAcpi->legIntersects[i];

			if (! pli->leg->bIsCrosswalk)
			{
				F32 fDistSQ;
				const F32 *pvlegPos = aiCivilianLegGetPos(pli->leg, iForward, false);
				if (vRelativeDirection)
				{
					subVec3(pvlegPos, vCivPos, vToLeg);
					fDot = dotVec3(vToLeg, vRelativeDirection);
					if (fDot > 0.0f)
						continue;
				}

				fDistSQ = lengthVec3Squared(vToLeg);
				if (fDistSQ < fBestDistSQ)
				{
					fBestDistSQ = fDistSQ;
					pBestLeg = pli->leg;
				}
			}
		}

		if (!pBestLeg)
		{
			// if we don't find a leg, just use the first one
			devassert(pAcpi->legIntersects);
			pBestLeg = pAcpi->legIntersects[0]->leg;
		}
	}
	else
	{
		PathLegIntersect *pli_edge, *pli_isect;
		aiCivMidIntersection_GetLegAndIsect(pAcpi, &pli_isect, &pli_edge);

		{
			if (pli_isect->leg->bIsCrosswalk)
			{
				pBestLeg = pli_edge->leg;
			}
			else if (vRelativeDirection)
			{
				subVec3(pli_isect->intersect, vCivPos, vToLeg);
				fDot = dotVec3(vToLeg, vRelativeDirection);
				if (fDot <= 0.0f)
				{
					pBestLeg = pli_edge->leg;
				}
				else
				{
					pBestLeg = pli_isect->leg;
				}
			}
			else
			{
				const F32 *pvLegPos;
				if (pli_isect->leg->prevInt == pAcpi)
					pvLegPos = pli_isect->leg->start;
				else 
					pvLegPos = pli_isect->leg->end;

				if (distance3Squared(pli_isect->intersect, vCivPos) < distance3Squared(pvLegPos, vCivPos))
				{
					pBestLeg = pli_edge->leg;
				}
				else
				{
					pBestLeg = pli_isect->leg;
				}
			}
		}
	}


	return pBestLeg;
}


// ------------------------------------------------------------------------------------------------------------------
static void aiPedestrianTurnAround(AICivilianPedestrian *civ, const Vec3 vCivPos, const Vec3 vRelativeDirection)
{
	AICivilianWaypoint *pWp;

	// destroy our current path.
	// reinitialize our current waypoint, in the opposite direction
	// call continue path.
	// resend our path to the BG

	pWp = aiCivilianClearPath_KeepCurWp(&civ->civBase);

	civ->civBase.forward = !civ->civBase.forward;

	if (! pWp->bIsLeg)
	{
		pWp->leg = aiCivFindClosestLegInIntersection(pWp->acpi, civ->civBase.forward, vCivPos, vRelativeDirection);
		devassert(pWp->leg);
		pWp->bIsLeg = true;
	}

	// remake the waypoint position, now that we've flipped the forward
	aiCivilianMakeWpPos(&civ->civBase, pWp->leg, pWp, 1);
	aiCivilianContinuePath(&civ->civBase);
	aiCiv_SendQueuedWaypoints(&civ->civBase);
}


// ------------------------------------------------------------------------------------------------------------------
// Recreates a path for the pedestrian given the leg
static void aiCivilianResetPath(Entity *e, AICivilianPedestrian *pCiv, const Vec3 vCurPos, const AICivilianPathLeg *pLeg)
{
	aiCivilian_ClearWaypointList(&pCiv->civBase);
	
	// make sure we're doing the simple civilian movement
	aiCivilianBeCheap(e, CIV_BASEPTR(pCiv));
	aiCivPedestrian_SetSpeedBasedOnState(e, pCiv);
	
	// if the civilian is not currently in the given leg, 
	if (! isPointInLegColumn(vCurPos, pLeg))
	{
		Vec3 vLegPos;
		AICivilianWaypoint *pWp;

		pWp = aiCivilianWaypointAlloc();
		pWp->leg = pLeg;
		pWp->bIsLeg = 1;
		pWp->distFromLeg = pLeg->width * 0.45f;

		acgPointLegDistSquared(vCurPos, pLeg, vLegPos);

		// check to see what side of the leg we should be going to
		{
			Vec3 vToLeg;
			subVec3(vLegPos, vCurPos, vToLeg);
			if (dotVec3(vToLeg, pLeg->perp) > 0.f)
			{
				pWp->distFromLeg = -pWp->distFromLeg;
			}
		}

		scaleAddVec3(pLeg->perp, pWp->distFromLeg, vLegPos, pWp->pos);
		
		aiCiv_AddNewWaypointToPath(&pCiv->civBase.path, pWp);
	}

	{
		AICivilianWaypoint *pWp;
		pWp = aiCivilianWaypointAlloc();
		pWp->leg = pLeg;
		pWp->bIsLeg = true;
		
		{
			const F32 *pvLegPos = aiCivilianLegGetPos(pLeg, pCiv->civBase.forward, true);
			scaleAddVec3(pLeg->perp, pCiv->distFromLeg, pvLegPos, pWp->pos);
		}	

		aiCiv_AddNewWaypointToPath(&pCiv->civBase.path, pWp);
	}
	
	aiCivilianContinuePath(&pCiv->civBase);
	
	// force an update
	pCiv->civBase.lastUpdateTime = ABS_TIME_PARTITION(pCiv->civBase.iPartitionIdx) - SEC_TO_ABS_TIME(s_fPed_UpdatePerSeconds);
	
	aiCiv_SendQueuedWaypoints(&pCiv->civBase);
}

// ------------------------------------------------------------------------------------------------------------------
// used when scared, make sure aren't scheduled to cross any crosswalks 
// since we can't wait for traffic while we're running in terror.
static void aiCivPedestrian_RemoveFutureCrosswalkWps(AICivilianPedestrian *pCiv, const Vec3 vCurPos)
{
	AICivilianWaypoint *pWp;

	if (pCiv->bIsUsingCrosswalk)
		return;

	pWp = aiCivilianGetCurrentWaypoint(&pCiv->civBase);

	if (pWp)
	{
		S32 x, numWaypts;
		bool bNeedRepath = false;

		if (pWp->bIsCrosswalk)// && pWp->bStop)
		{
			S32 prevCount = 3;

			pWp = NULL;
			x = pCiv->civBase.path.curWp;
			while(--x >= 0 && --prevCount)
			{
				AICivilianWaypoint *pPrevWp = pCiv->civBase.path.eaWaypoints[x];
				if (pPrevWp && !pPrevWp->bIsCrosswalk)
				{
					pWp = pPrevWp;
					break;
				}
			}

			// civ is waiting for crosswalk, but just got scared. 
			// we have to destroy the path and repath
			if (pWp)
			{
				const AICivilianPathLeg *pLeg;
				if (! pWp->bIsLeg)
				{
					pLeg = aiCivFindClosestLegInIntersection(pWp->acpi, pCiv->civBase.forward, vCurPos, NULL);
				}
				else
				{
					pLeg = pWp->leg;
				}

				if (pLeg)
				{
					Entity *e = entFromEntityRefAnyPartition(pCiv->civBase.myEntRef);
					if (e) aiCivilianResetPath(e, pCiv, vCurPos, pLeg);
				}
			}
			return;
		}
		else
		{
			// check the rest of the path to see if there are any crosswalks to cross
			numWaypts = eaSize(&pCiv->civBase.path.eaWaypoints);
			for (x = pCiv->civBase.path.curWp + 1; x < numWaypts; x++)
			{
				ANALYSIS_ASSUME(pCiv->civBase.path.eaWaypoints);
				pWp = pCiv->civBase.path.eaWaypoints[x];
				if (pWp->bIsCrosswalk)
				{
					bNeedRepath = true;
					break;
				}
			}

			if (bNeedRepath)
			{
				pWp = aiCivilianClearPath_KeepCurWp(&pCiv->civBase);

				if (! pWp->bIsLeg)
				{
					pWp->leg = aiCivFindClosestLegInIntersection(pWp->acpi, pCiv->civBase.forward, vCurPos, NULL);
					devassert(pWp->leg);
					pWp->bIsLeg = true;
				}

				aiCivilianContinuePath(&pCiv->civBase);
				aiCiv_SendQueuedWaypoints(&pCiv->civBase);
			}
		}

	}
}


//
// ------------------------------------------------------------------------------------------------------------------
void aiCivScarePedestrian(Entity *e, Entity *scarer, const Vec3 pvScarePos)
{
	/*
	If we are currently scared, check our last scare time.
	Get the direction of the scare.
	Calculate our current direction.
	If we are heading towards the scare direction, reinitialize a path away from the scare
	Start Running!
	Set scared state and set time.
	*/
	AICivilianPedestrian *civ;
	U32 currentTime;
	
	if (!scarer && !pvScarePos)
		return;

	if(!e->pCritter || !e->pCritter->civInfo || !entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY))
		return;
	
	if (e->pCritter->civInfo->type != EAICivilianType_PERSON)
		return;
	
	civ = (AICivilianPedestrian*)e->pCritter->civInfo;
	if (civ->bScareReceived)
		return; // already scared
	
	{
		Vec3 vScarePos;
		Vec3 vCivPos;
		const AICivPedestrianScaredParams* pScaredParams = aiCivPedestrian_GetScaredParams();
		
		currentTime = timeSecondsSince2000();
		if ( (currentTime - civ->lastScareTime) < pScaredParams->fRescareTime)
			return; // scared too recently
	
		// get the scare position, and also check the vertical distance 
		if (!pvScarePos) {
			entGetPos(scarer, vScarePos);
		} else {
			copyVec3(pvScarePos, vScarePos);
		}
			
		entGetPos(e, vCivPos);
		{
			F32 vertdist = vCivPos[1] - vScarePos[1];
			if (ABS(vertdist) > pScaredParams->fScaredHeight)
				return;
		}
			
		civ->s16ScarePos[0] = (S16)floor(vScarePos[0]);
		civ->s16ScarePos[1] = (S16)floor(vScarePos[2]);
	}

	civ->bScareReceived = true;
	civ->erScarer = (scarer) ? entGetRef(scarer) : 0;


	
}

static void aiCivPedestrian_DoScare(Entity *e, AICivilianPedestrian *civ, const Vec2 vCivPos)
{
	
	AICivPedestrianDef *civDef;
	const AICivPedestrianScaredParams* pScaredParams;

	if (!civ->bScareReceived)
		return;

	civ->bScareReceived = 0;

	civDef = (AICivPedestrianDef*)AI_CIV_DEF(civ);
	pScaredParams = aiCivPedestrian_GetScaredParams();

	if (civDef->pExprOnScared || pScaredParams->pExprOnScared)
	{	// if we have an expression to run when we're scared, just do it now
		Entity *pScarerEnt = civ->erScarer ? entFromEntityRefAnyPartition(civ->erScarer) : NULL;
		MultiVal answer = {0};

		aiCivPedestrian_ExprVarsAdd(e->aibase->exprContext, pScarerEnt);
		exprEvaluate(	(civDef->pExprOnScared) ? civDef->pExprOnScared : pScaredParams->pExprOnScared, 
						e->aibase->exprContext, &answer);
		aiCivPedestrian_ExprVarsRemove(e->aibase->exprContext);
	}

	// if we have a fear behavior FSM override, trigger a callout and skip normal scared behavior. 
	if (civDef->pchFearBehaviorFSMOverride)
	{
		civ->calloutInfo.entCalloutEntity = civ->erScarer;
		if (aiCivilianRunFSMByName(e, civ, civDef->pchFearBehaviorFSMOverride))
			return;
	}

	// check to see if we should say that we're scared
	if (randomChance(pScaredParams->fMessageChance))
	{
		const char *pszMessage = aiCivPedestrian_GetScaredMessage();
		if (pszMessage && pszMessage[0])
		{
			aiSayMessageInternal(e, NULL, NULL, pszMessage, NULL, s_fCivMessageDuration);
		}
	}

	{
		int bShouldRetargetPath = !(aiCivPedestrian_IsGoingToPos(civ) || aiCivPedestrian_IsPathExhausted(civ));
	
		// set the scared flag and timers
		civ->lastScareTime = timeSecondsSince2000();
		aiCivPedestrian_SetState(civ, ECivilianState_SCARED);

		if (bShouldRetargetPath)
		{
			bool bTurnAround = false;
			Vec3 vToScarePos;
			Vec3 vScarePos;
			
			setVec3(vScarePos, (F32)civ->s16ScarePos[0], vCivPos[1], (F32)civ->s16ScarePos[1]);
			subVec3(vScarePos, vCivPos, vToScarePos);
			normalVec3(vToScarePos);

			// check to see if we need to turn around
			if (civ->bIsUsingCrosswalk == false)  // don't turn around if we're using a crosswalk
			{
				Vec3 vFacingDir;
				Vec2 pyFace;
				F32 fDot;

				entGetFacePY(e, pyFace);
				setVec3FromYaw(vFacingDir, pyFace[1]);
				//setVec3FromYaw(vFacingDir, 0.f);

				fDot = dotVec3XZ(vFacingDir, vToScarePos);
				bTurnAround = (fDot > cos(RAD(50.f))); // only turn around if in the front 50degree arc
			}

			// check to see if we need to turn around
			if (aiCivPedestrian_IsPathExhausted(civ))
			{
				AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e)); 
				AICivilianPathLeg *leg = aiCivilian_GetLegByPos(pPartition->pWorldLegGrid, EAICivilianType_PERSON, vCivPos, NULL);
				if (leg)
					aiCivilianResetPath(e, civ, vCivPos, leg);
			}
			else if (bTurnAround)
			{	
				aiPedestrianTurnAround(civ, vCivPos, vToScarePos);
			}
			else
			{
				aiCivPedestrian_RemoveFutureCrosswalkWps(civ, vCivPos);
			}

			// check to see if we should cower in fear
			if (civ->bIsUsingCrosswalk == false) // don't cower if we're using a crosswalk
			{
				if (bTurnAround && randomChance(pScaredParams->fCowerChance))
				{
					aiCivilianPauseMovement(&civ->civBase, true);
				}
				else
				{	// make sure we are not paused
					aiCivilianPauseMovement(&civ->civBase, false);
				}
			}
		}
	}

}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_SetAnimList(Entity *e, AICivilianPedestrian *civ, AIAnimList *pAnimList)
{
	if (civ->animListCmdQueue)
	{
		CommandQueue_ExecuteAllCommands(civ->animListCmdQueue);
	}

	if (pAnimList)
	{
		aiAnimListSet(e, pAnimList, &civ->animListCmdQueue);
	}
	else if (civ->animListCmdQueue)
	{
		CommandQueue_Destroy(civ->animListCmdQueue);
		civ->animListCmdQueue = NULL;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_SetAnimListByName(Entity *e, AICivilianPedestrian *civ, const char *pszAnimList)
{
	AIAnimList *pAnimList = NULL;
	if (pszAnimList)
	{
		pAnimList = RefSystem_ReferentFromString(g_AnimListDict, pszAnimList);
	}

	aiCivPedestrian_SetAnimList(e, civ, pAnimList);
}



// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_SetAnimBit(Entity *e, AICivilianPedestrian *civ, const char *pszBitName)
{
	U32 mmBitHandle = mmGetAnimBitHandleByName(pszBitName, false);
	S32 handle;

	if (civ->animListCmdQueue)
	{
		CommandQueue_ExecuteAllCommands(civ->animListCmdQueue);
	}

	if(! civ->animListCmdQueue)
		civ->animListCmdQueue = CommandQueue_Create(32, false);

	aiMovementAddAnimBitHandle(e, mmBitHandle, &handle);
	QueuedCommand_aiAnimListRemoveAnim(civ->animListCmdQueue, e, CIV_ANIMBIT_CMDQUEUE_NAME, handle);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_ReAddAllWaypoints(AICivilianPedestrian *pPed)
{
	AICivilianPath *pPath = &CIV_BASE(pPed).path;

	if (pPath->eaWaypoints)
	{
		S32 i;
				
		CIV_BASE(pPed).path.waypointClearID = mmAICivilianClearWaypoints(CIV_BASE(pPed).requester);

		eaClear(&pPath->eaAddedWaypoints);

		for (i = pPath->curWp; i < eaSize(&pPath->eaWaypoints); i++)
		{
			AICivilianWaypoint *wp = pPath->eaWaypoints[i];
			eaPush(&pPath->eaAddedWaypoints, wp);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
// callback for when the pedestrian has completed its movement when moving back to a leg
static void aiCivPedestrian_ReacquirePathComplete(Entity *e, AICivilianPedestrian *civ, 
												  const Vec3 vCurPos, int bPathTimedOut)
{
	AICivilianPathLeg *leg;
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e)); 

	leg = aiCivilian_GetLegByPos(pPartition->pWorldLegGrid, civ->civBase.type, vCurPos, NULL);
	if (!leg)
		return;
		
	aiCivilianResetPath(e, civ, vCurPos, leg);
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivCollideRay(int iPartitionIdx, const Vec3 sourcePos, const Vec3 targetPos)
{
	Vec3 castSrcPos;
	Vec3 castTargetPos;
	WorldCollCollideResults results = {0};

	copyVec3(sourcePos, castSrcPos);
	copyVec3(targetPos, castTargetPos);
	castSrcPos[1] += 2.f;
	castTargetPos[1] += 2.f;

	return worldCollideRay(iPartitionIdx, castSrcPos, castTargetPos, WC_QUERY_BITS_AI_CIV, &results);
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivPedestrian_PathToUpdate(Entity *e, AICivilianPedestrian *ped, const Vec3 curPos)
{
	int partitionIdx = entGetPartitionIdx(e);
	if (aiCivilian_IsDoingCivProcessingOnly(e, CIV_BASEPTR(ped)))
	{
		AICivilianPath *path;
		S32 numReached;

		numReached = mmAICivilianGetAndClearReachedWp(CIV_BASE(ped).requester);
		path = &CIV_BASE(ped).path;
		
		path->curWp += numReached;
		if (path->curWp >= eaSize(&path->eaWaypoints))
		{
			Vec2 pyFace;
			entGetFacePY(e, pyFace);
			if (subAngle(pyFace[1], ped->fTargetRot) > RAD(5.f))
				return false;

			if (ped->fpPathingStateComplete)			
			{
				if (ped->bSnapToTarget)
				{
					F32 fDistSQ = distance3SquaredXZ(curPos, ped->vTargetPathPos);
					if (fDistSQ > SQR(1.75f))
						entSetPos(e, ped->vTargetPathPos, true, "POI repositioning");
				}

				ped->fpPathingStateComplete(e, ped, curPos, false);
				ped->fpPathingStateComplete = NULL;
				aiCivilian_ClearWaypointList(CIV_BASEPTR(ped));
			}

			return true;
		}
	}
	else
	{
		int bDonePathing =  !e->aibase->currentlyMoving && 
								(distance3SquaredXZ(ped->vTargetPathPos, curPos) <= SQR(3.f));
		int bTimedOut = (ABS_TIME_SINCE_PARTITION(partitionIdx, ped->startPathTime) > SEC_TO_ABS_TIME(15.f));

		// check if we have arrived.
		if (bDonePathing || bTimedOut) 
		{
			Vec2 pyFace;
			entGetFacePY(e, pyFace);
			if (!bTimedOut && subAngle(pyFace[1], ped->fTargetRot) > RAD(5.f))
				return false;

			if (ped->fpPathingStateComplete)			
			{
				if (ped->bSnapToTarget)
				{
					F32 fDistSQ = distance3SquaredXZ(curPos, ped->vTargetPathPos);
					if (fDistSQ > SQR(1.75f))
						entSetPos(e, ped->vTargetPathPos, true, "POI repositioning");
				}

				ped->fpPathingStateComplete(e, ped, curPos, !bDonePathing && bTimedOut);
				ped->fpPathingStateComplete = NULL;
			}
			return true;
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivPedestrian_NormalPathTo(Entity *e, AICivilianPedestrian *ped, 
											const Vec3 vTargetPos, F32 fTargetRot, 
											fpPathingComplete fp, bool bSnapToTarget )
{
	int partitionIdx = entGetPartitionIdx(e);
	copyVec3(vTargetPos, ped->vTargetPathPos);
	ped->fTargetRot = fTargetRot;
	// civ->fpPathingStateUpdate = fp;

	// make sure we are doing normal critter movement
	aiCivilianBeNormal(e, CIV_BASEPTR(ped));
	aiCivPedestrian_SetSpeedBasedOnState(e, ped);

	// aiCivPedestrian_SetState(civ, ECivilianState_PATHING);

	if (! aiMovementSetTargetPosition(e, e->aibase, vTargetPos, NULL, 0) )
	{
		// failed to path
		ped->fpPathingStateComplete = NULL;
		aiCivilianBeCheap(e, CIV_BASEPTR(ped));
		aiCivPedestrian_SetSpeedBasedOnState(e, ped);
		// aiCivPedestrian_SetState(civ, ECivilianState_DEFAULT);
		return false;
	}

	
	aiMovementSetWalkRunDist(e, e->aibase, 1.f, 80.f, 0);

	{
		Quat rot;
		yawQuat(-fTargetRot, rot);
		aiMovementSetFinalFaceRot(e, e->aibase, rot);
	}
	

	ped->startPathTime = ABS_TIME_PARTITION(partitionIdx);
	ped->fpPathingStateComplete = fp;
	ped->bSnapToTarget = bSnapToTarget;

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static bool aiCivPedestrian_CheapPathTo(Entity *e, AICivilianPedestrian *ped, 
										const Vec3 vTargetPos, F32 fTargetRot,
										fpPathingComplete fp, 
										bool bSnapToTarget)
{
	copyVec3(vTargetPos, ped->vTargetPathPos);
	ped->fTargetRot = fTargetRot;

	// clear our waypoint list and make sure we're doing regular pedestrian movement
	aiCivilian_ClearWaypointList(CIV_BASEPTR(ped));
	aiCivilianBeCheap(e, CIV_BASEPTR(ped));
	aiCivPedestrian_SetSpeedBasedOnState(e, ped);
	
	{
		AICivilianWaypoint *pWayPoint = aiCivilianWaypointAlloc();
		copyVec3(vTargetPos, pWayPoint->pos);
		aiCiv_AddNewWaypointToPath(&CIV_BASE(ped).path, pWayPoint);
		aiCiv_SendQueuedWaypoints(CIV_BASEPTR(ped));
	}

	mmAICivilianMovementSetFinalFaceRot(CIV_BASE(ped).requester, fTargetRot);
	ped->fpPathingStateComplete = fp;
	ped->bSnapToTarget = bSnapToTarget;
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivPedestrian_GotoPosition(Entity *e, AICivilianPedestrian *civ, 
										const Vec3 targetPos, F32 targetRot, 
										fpPathingComplete fp, bool bSnapToTarget)
{
	Vec3 curPos;
	int iPartitionIdx = entGetPartitionIdx(e);

	entGetPos(e, curPos);
	civ->bFailedPath = false;

	if (aiCivCollideRay(iPartitionIdx, curPos, targetPos))
	{	// something in the way, we need to do a normal critter pathfind
		
		return aiCivPedestrian_NormalPathTo(e, civ, targetPos, targetRot, fp, bSnapToTarget);
	}
	else
	{	// we should be able to go straight to the position
		return aiCivPedestrian_CheapPathTo(e, civ, targetPos, targetRot, fp, bSnapToTarget);
	}

	
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivPedestrian_IsGoingToPos(AICivilianPedestrian *civ)
{
	return civ->fpPathingStateComplete != NULL;
}


// ------------------------------------------------------------------------------------------------------------------
static int aiCivPedestrian_ReturnToNearestLeg(Entity *e, AICivilianPedestrian *civ)
{
	Vec3 curPos;
	Vec3 vLegPos;
	AICivilianPathLeg *leg;
	int iPartitionIdx = entGetPartitionIdx(e);
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx); 

	entGetPos(e, curPos);

	civ->bFailedPath = false;

	leg = aiCivilian_GetLegByPos(pPartition->pWorldLegGrid, civ->civBase.type, curPos, vLegPos);
	if(!leg)
		return false;

	if (aiCivCollideRay(iPartitionIdx, curPos, vLegPos))
	{	// something is in the way, we have to pathfind 
		if (!aiCivPedestrian_NormalPathTo(e, civ, vLegPos, 0.f, aiCivPedestrian_ReacquirePathComplete, false))
		{
			civ->fpPathingStateComplete = NULL;
			civ->distFromLeg = randomF32() * ((leg->width * 0.5f) - s_fPedestrianColRadius);
			aiCivilianResetPath(e, civ, curPos, leg);
		}
	}
	else
	{	// do normal civilian 
		civ->fpPathingStateComplete = NULL;

		civ->distFromLeg = randomF32() * ((leg->width * 0.5f) - s_fPedestrianColRadius);
		aiCivilianResetPath(e, civ, curPos, leg);
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool aiCivPedestrian_CanUseCrosswalk(const AICivilianPedestrian *pCiv, const AICivilianPathLeg *pXWalk)
{
	if (pCiv->eState != ECivilianState_SCARED)
	{
		return aiCivCrosswalk_CanCivUseCrosswalk(pXWalk);
	}
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int _validateForLegDef(const AICivilianPathLeg *pLeg, const AICivilianPathLeg *pCurLeg)
{
	// if we are on a leg that has the same leg def, don't recheck the use chance
	if (pLeg->pLegDef && pCurLeg->pLegDef != pLeg->pLegDef)
	{
		return randomChance(pLeg->pLegDef->fLegUseChance);
	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static bool aiCivPedestrian_ValidateACPI(const AICivilianPedestrian *pCiv, AICivilianPathIntersection *acpi, const AICivilianWaypoint *wp)
{
	bool bValid = false;
	S32 x = eaSize(&acpi->legIntersects) - 1;
	ANALYSIS_ASSUME(acpi->legIntersects);
	do {
		PathLegIntersect *pli = acpi->legIntersects[x];

		if (pli->leg == wp->leg || (wp->leg->leg_set && (pli->leg->leg_set == wp->leg->leg_set)))
		{
			pli->continue_isvalid = false;
			continue;
		}

		if (pli->leg->bIsCrosswalk && !aiCivPedestrian_CanUseCrosswalk(pCiv, pli->leg))
		{
			pli->continue_isvalid = false;
			continue;
		}

		if (!_validateForLegDef(pli->leg, wp->leg))
		{
			pli->continue_isvalid = false;
			continue;
		}

		pli->continue_isvalid = true;
		bValid = true;

	} while (--x >= 0);

	return bValid;
}

// ------------------------------------------------------------------------------------------------------------------
bool pedValidatePathLegMidIntersections(const AICivilianPedestrian *civ, 
										const AICivilianWaypoint *wp, 
										const AICivilianWaypoint *prev_wp, 
										AICivilianPathIntersection *acpi, 
										bool forward)
{
	PathLegIntersect *leg_pli, *isect_pli;

	aiCivMidIntersection_GetLegAndIsect(acpi, &isect_pli, &leg_pli);
	if (isect_pli && leg_pli)
	{
		Vec3 vWayPtToIntersect;
		F32 fDot;

		if (prev_wp == NULL)
			return false;

		if (prev_wp->acpi == acpi) // (unnessesary to check this, the pointer compare alone should be fine) !prev_wp->bIsLeg && 
		{
			// don't take the same mid intersection twice!
			return false;
		}

		if (prev_wp->bIsCrosswalk && prev_wp->leg == isect_pli->leg)
			return false;

		// this mid intersection is somewhere on the middle of the leg.
		// make sure that the mid intersection is ahead, and not behind us.
		subVec3(isect_pli->intersect, prev_wp->pos, vWayPtToIntersect);
		fDot = dotVec3(vWayPtToIntersect, wp->leg->dir);
		if ( (forward && fDot < 0) || (!forward && fDot > 0))
		{
			// this waypoint is 'behind' us, we cannot use it
			return false;
		}

		if (isect_pli->leg->bIsCrosswalk)
		{
			if (prev_wp->leg != isect_pli->leg && !aiCivPedestrian_CanUseCrosswalk(civ, isect_pli->leg))
				return false;
		}

		if (!_validateForLegDef(isect_pli->leg, wp->leg))
			return false;

		return true;
	}

	return false;
}



// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool isPointInLegColumn(const Vec3 vPos, const AICivilianPathLeg *leg)
{
	F32 perpDist;
	Vec3 vStartToPos;

	subVec3(vPos, leg->start, vStartToPos);
	perpDist = fabs(dotVec3(vStartToPos, leg->perp));
	perpDist = perpDist - (leg->width * 0.5f);
	return perpDist <= 0.001f;
}



// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool clipLineSegToLegColumn(Vec3 lineSeg_pt, Vec3 lineSeg_dirLen, const Vec3 columnPt1, const Vec3 columnPt2, const Vec3 columnPt1Norm)
{
	F32 dotDir, dot2, t;
	Vec3 vDir;

	dotDir = dotVec3(lineSeg_dirLen, columnPt1Norm);
	if (dotDir == 0) return false; // the line is parallel

	subVec3(lineSeg_pt, columnPt1, vDir);
	dot2 = -dotVec3(vDir, columnPt1Norm);
	t = dot2 / dotDir;

	if (t > 0.0f && t < 1.0f)
	{
		// the intersection point is within the line segment
		if (dot2 < 0.0f)
		{
			// clip the start point
			Vec3 isectPt;
			scaleAddVec3(lineSeg_dirLen, t, lineSeg_pt, isectPt);

			copyVec3(isectPt, lineSeg_pt);

			// recalculate the lineSeg_dirLen
			t = 1.0f - t;
			scaleVec3(lineSeg_dirLen, t, lineSeg_dirLen);
		}
		else
		{
			// recalculate the lineSeg_dirLen
			scaleVec3(lineSeg_dirLen, t, lineSeg_dirLen);
		}


	}

	dotDir = -dotDir;
	subVec3(lineSeg_pt, columnPt2, vDir);
	scaleVec3(vDir, -1.0f, vDir);
	dot2 = -dotVec3(vDir, columnPt1Norm);
	t = dot2 / dotDir;

	if (t > 0.0f && t < 1.0f)
	{
		if (dot2 < 0.0f)
		{
			// clip the start point
			Vec3 isectPt;
			scaleAddVec3(lineSeg_dirLen, t, lineSeg_pt, isectPt);
			copyVec3(isectPt, lineSeg_pt);
			// recalculate the lineSeg_dirLen
			t = 1.0f - t;
			scaleVec3(lineSeg_dirLen, t, lineSeg_dirLen);
		}
		else
		{
			// recalculate the lineSeg_dirLen
			scaleVec3(lineSeg_dirLen, t, lineSeg_dirLen);
		}


	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool clipLegPerpToLegColumn(const AICivilianPathLeg *legPerp, const AICivilianPathLeg *legCol, Vec3 vSegPt1, Vec3 vSegPt2, bool perpStart)
{
	Vec3 vDirLen, vColPos1, vColPos2;
	F32 hwidth = -legPerp->width * 0.5f;
	const F32 *pvLegPos;

	pvLegPos = aiCivilianLegGetPos(legPerp, perpStart, true);

	scaleAddVec3(legPerp->perp, hwidth, pvLegPos, vSegPt1);

	scaleVec3(legPerp->perp, legPerp->width, vDirLen);

	hwidth = legCol->width * 0.5f;
	scaleAddVec3(legCol->perp, hwidth, legCol->start, vColPos1);
	hwidth = -hwidth;
	scaleAddVec3(legCol->perp, hwidth, legCol->start, vColPos2);

	clipLineSegToLegColumn(vSegPt1, vDirLen, vColPos1, vColPos2, legCol->perp);

	addVec3(vSegPt1, vDirLen, vSegPt2);
	return isPointInLegColumn(vSegPt1, legCol);
}


// ------------------------------------------------------------------------------------------------------------------
static void civPedContinueInLegColumn(const AICivilianWaypoint *wp, AICivilianWaypoint *nextWp, S32 future_forward)
{
	const F32* pvLegPos;
	Vec3 vDir;

	pvLegPos = aiCivilianLegGetPos(nextWp->leg, future_forward, true);

	// get the actual distance from the leg
	subVec3(wp->pos, pvLegPos, vDir);
	nextWp->distFromLeg = dotVec3(nextWp->leg->perp, vDir);

	// randomly offset this point by some amount
	{
#define OFFSET_PER_LEGLEN_FACTOR (1.0f/10.0f)
		F32 hwidth;
		F32 offsetAmount = (nextWp->leg->len * OFFSET_PER_LEGLEN_FACTOR) * randomF32();

		nextWp->distFromLeg += offsetAmount;

		hwidth = (nextWp->leg->width - 2.5f) * 0.5f;
		if (hwidth < 0.f) hwidth = 1.f;
		nextWp->distFromLeg = CLAMP(nextWp->distFromLeg, -hwidth, hwidth);

		scaleAddVec3(nextWp->leg->perp, nextWp->distFromLeg, pvLegPos, nextWp->pos);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedContinuePath_LegToLeg(AICivilianPedestrian *civ, AICivilianWaypoint *wp, const AICivilianPathLeg *pLeg, S32 future_forward)
{
	AICivilianWaypoint *nextWp = aiCivilianWaypointAlloc();

	nextWp->bIsLeg = true;
	nextWp->leg = pLeg;

	// first, legs see if we need to move the current waypoint to be in line with the next waypoint
	if (!isPointInLegColumn(wp->pos, nextWp->leg))
	{
		Vec3 vPt1, vPt2;
		// the previous leg is not within the next leg's column.
		// move our current waypoint to line up with the next leg column.

		// Get the overlapping leg segment between the two legs
		if (clipLegPerpToLegColumn(wp->leg, nextWp->leg, vPt1, vPt2, future_forward))
		{
			// there is some overlap.
			F32 frandInterp = randomPositiveF32();
			frandInterp = CLAMP(frandInterp, 0.1f, 0.9f);
			interpVec3(frandInterp, vPt1, vPt2, wp->pos);

		}
	}

	nextWp->bReverse = !calcLegFlow(wp->leg, nextWp->leg);
	if (nextWp->bReverse)
		future_forward = !future_forward;

	if (isPointInLegColumn(wp->pos, nextWp->leg))
	{
		civPedContinueInLegColumn(wp, nextWp, future_forward);
	}
	else
	{
		aiCivilianMakeWpPos(&civ->civBase, nextWp->leg, nextWp, true);
	}

	aiCiv_AddNewWaypointToPath(&civ->civBase.path, nextWp);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPedestrian_ContinueOntoCrosswalk(AICivilianPedestrian *pCiv, const AICivilianPathIntersection *pAcpi, const AICivilianPathLeg *pXWalkLeg, S32 iFutureForward, bool bReverse)
{
	AICivilianWaypoint *pEndofXWalkWp;

	pEndofXWalkWp = aiCivilianWaypointAlloc();

	if (bReverse) 
		iFutureForward = !iFutureForward;

	// initialize the end of the crosswalk waypoint
	pEndofXWalkWp->leg = pXWalkLeg;
	pEndofXWalkWp->bStop = true;
	pEndofXWalkWp->bIsCrosswalk = true;
	pEndofXWalkWp->bIsLeg = true;
	pEndofXWalkWp->pCrosswalkUser = aiCivCrosswalk_CreateAddUser((AICivilianPathLeg*)pXWalkLeg, &pCiv->civBase, iFutureForward);
	devassert(pEndofXWalkWp->pCrosswalkUser);

	// create the waypoint before the start of the crosswalk
	{
		AICivilianWaypoint *pWaitingWp;
		pWaitingWp = aiCivilianWaypointAlloc();

		// if we're coming from an intersection
		if (pAcpi)
		{
			pWaitingWp->acpi = pAcpi;
		}
		else
		{
			pWaitingWp->leg = pXWalkLeg;
			pWaitingWp->bIsLeg = true;
		}

		pWaitingWp->bIsCrosswalk = true;
		pWaitingWp->bReverse = bReverse;

		aiCivCrosswalk_SetWaypointWaitingPos(pEndofXWalkWp, pWaitingWp);

		aiCiv_AddNewWaypointToPath(&pCiv->civBase.path, pWaitingWp);
	}


	// initialize the end of the crosswalks' waypoint position
	{
		const F32 *pvLegPos = aiCivilianLegGetPos(pXWalkLeg, iFutureForward, true);	
		F32 fRandOffset = randomF32() * pXWalkLeg->width * 0.5f;

		scaleAddVec3(pXWalkLeg->perp, fRandOffset, pvLegPos, pEndofXWalkWp->pos);

		aiCiv_AddNewWaypointToPath(&pCiv->civBase.path, pEndofXWalkWp);
	}
}



// ------------------------------------------------------------------------------------------------------------------
static const AICivilianPathLeg* aiCivPedestrian_GetBestLegACPI(const AICivilianPathIntersection *acpi, const Vec3 prevPos)
{
	PathLegIntersect *pli;
	S32 x;
	x = eaSize(&acpi->legIntersects) - 1;
	devassert(x >= 0);

	do {
		pli = acpi->legIntersects[x];
		if (pli->continue_isvalid)
		{
			if (pli->leg->leg_set != 0 && isPointInLegColumn(prevPos, pli->leg))
			{
				return pli->leg;
			}
		}

	} while(--x >= 0);

	{
		pli = acpi->legIntersects[randomIntRange(0, eaSize(&acpi->legIntersects)-1)];
		if (pli->continue_isvalid)
			return pli->leg;

		x = eaSize(&acpi->legIntersects) - 1;
		do {
			pli = acpi->legIntersects[x];
			if (! pli->continue_isvalid)
			{
				continue;
			}

			return pli->leg;

		} while (--x >= 0);
	}

	return acpi->legIntersects[0]->leg;
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static void civPedestrianGetMidWaypointPos(const AICivilianPathLeg *edgeLeg, const PathLegIntersect *isectPli,
														 F32 fDistFromLeg, Vec3 vOutPos)
{
	F32 hwidth = edgeLeg->width * 0.5f;
	Vec3 vTmp;

	subVec3(isectPli->leg->start, isectPli->intersect, vTmp);
	if (dotVec3(vTmp, edgeLeg->perp) < 0.0f)
	{
		hwidth = -hwidth;
	}

	scaleAddVec3(edgeLeg->perp, hwidth, isectPli->intersect, vTmp);
	scaleAddVec3(isectPli->leg->perp, fDistFromLeg, vTmp, vOutPos);
}


// ------------------------------------------------------------------------------------------------------------------
bool aiCivPedestrian_ContinuePath(AICivilianPedestrian *civ)
{
	AICivilianWaypoint *wp;	
	AICivPathSelectionOutput	pathDest;
	S32 future_forward;

	wp = aiCivilianGetFromLastWaypoint(&civ->civBase, 0);
	devassert(wp);

	if(! wp->bIsLeg)
		return false;

	future_forward  = aiCivilianCalculateFutureForward(&civ->civBase);

	// find a random leg/intersection to traverse to
	{
		static AICivilianPathIntersection **s_validMids = NULL;

		AICivPathSelectionInput		pathSel;

		pathDest.pLeg = NULL;
		pathDest.pAcpi = NULL;

		pathSel.pLeg = future_forward  ? wp->leg->next : wp->leg->prev;
		pathSel.pAcpi = future_forward  ? wp->leg->nextInt : wp->leg->prevInt;
		pathSel.eaMids = &s_validMids;
		eaClear(&s_validMids);

		// validate and build a list of all the possible connections that we can get to
		{
			AICivilianPathIntersection **eaMids = wp->leg->midInts;

			if (pathSel.pLeg)
			{
				if (pathSel.pLeg->bIsCrosswalk && !aiCivPedestrian_CanUseCrosswalk(civ, pathSel.pLeg))
				{
					pathSel.pLeg = NULL;
				}
				else if (!_validateForLegDef(pathSel.pLeg, wp->leg)) 
				{
					pathSel.pLeg = NULL;
				}
			}

			if (pathSel.pAcpi)
			{
				if (! aiCivPedestrian_ValidateACPI(civ, (AICivilianPathIntersection*)pathSel.pAcpi, wp))
					pathSel.pAcpi = NULL;
			}

			if (eaMids)
			{	// validate our mid-intersections
				S32 i;
				AICivilianWaypoint *prevWp = aiCivilianGetFromLastWaypoint(&civ->civBase, 1);

				i = eaSize(&eaMids);
				while(--i >= 0)
				{
					AICivilianPathIntersection *midACPI = eaMids[i];
					if (pedValidatePathLegMidIntersections(civ, wp, prevWp, midACPI, future_forward))
					{
						eaPush(&s_validMids, midACPI);
					}
				}
			}
		}

		if(pathSel.pLeg || pathSel.pAcpi || eaSize(&s_validMids))
		{	
			// from our available choices, choose a random leg or intersection to traverse
			civContinuePath_GetRandomPath(&pathSel, &pathDest);	
		}

	}

	if(!pathDest.pAcpi && !pathDest.pLeg)
	{	// reached the end of a leg, turn around
		AICivilianWaypoint *nextWp = aiCivilianWaypointAlloc();

		nextWp->bIsLeg = true;
		nextWp->leg = wp->leg;
		nextWp->bReverse = true;		// We're turning around
		nextWp->bStop = randomChance(0.3f);

		aiCivilianMakeWpPos(&civ->civBase, nextWp->leg, nextWp, true);
		aiCiv_AddNewWaypointToPath(&civ->civBase.path, nextWp);
		return true;
	}

	if(pathDest.pLeg)
	{
		if (pathDest.pLeg->bIsCrosswalk == false)
		{
			aiCivPedContinuePath_LegToLeg(civ, wp, pathDest.pLeg, future_forward);	
		}
		else
		{
			bool bReverse = !calcLegFlow(wp->leg, pathDest.pLeg);
			aiCivPedestrian_ContinueOntoCrosswalk(civ, NULL, pathDest.pLeg, future_forward, bReverse);
		}
		return true;
	}

	// we are using an intersection
	devassert(pathDest.pAcpi);
	if(!pathDest.pAcpi->bIsMidIntersection)
	{
		const AICivilianPathLeg *pNextLeg;

		pNextLeg = aiCivPedestrian_GetBestLegACPI(pathDest.pAcpi, wp->pos);
		devassert(pNextLeg);

		if (pNextLeg->bIsCrosswalk)
		{
			bool bReverse = !calcLegFlow(wp->leg, pNextLeg);
			aiCivPedestrian_ContinueOntoCrosswalk(civ, pathDest.pAcpi, pNextLeg, future_forward, bReverse);
			return true;
		}

		if (!isPointInLegColumn(wp->pos, pNextLeg)) // todo: along with the leg column check, check the angle difference between the legs.
		{
			AICivilianWaypoint *pNextWp;

			// create the waypoint that takes us to the beginning of the next leg
			{
				pNextWp = aiCivilianWaypointAlloc();
				pNextWp->acpi = pathDest.pAcpi;
				pNextWp->bReverse = !calcLegFlow(wp->leg, pNextLeg);
				aiCivilianMakeWpPos(&civ->civBase, pNextLeg, pNextWp, false);
				aiCiv_AddNewWaypointToPath(&civ->civBase.path, pNextWp);
			}

			// create the waypoint that takes us to the end of the next leg
			{
				pNextWp = aiCivilianWaypointAlloc();
				pNextWp->bIsLeg = true;
				pNextWp->leg = pNextLeg;
				aiCivilianMakeWpPos(&civ->civBase, pNextLeg, pNextWp, true);
				aiCiv_AddNewWaypointToPath(&civ->civBase.path, pNextWp);
			}
		}
		else
		{
			// these waypoints line up, so we're going to skip the waypoint at the start
			// of this intersection
			AICivilianWaypoint *pNextWp = aiCivilianWaypointAlloc();

			pNextWp->bIsLeg = true;
			pNextWp->leg = pNextLeg;
			pNextWp->bReverse = !calcLegFlow(wp->leg, pNextLeg);

			if (pNextWp->bReverse)
				future_forward = !future_forward;

			civPedContinueInLegColumn(wp, pNextWp, future_forward);
			aiCiv_AddNewWaypointToPath(&civ->civBase.path, pNextWp);
		}

		return true;
	}
	else 
	{	// Mid-point intersection
		PathLegIntersect *legPli = NULL;
		PathLegIntersect *intPli = NULL;

		{
			S32 i = 1;
			devassert(eaSize(&pathDest.pAcpi->legIntersects) == MAX_PATHLEG_MID_INTERSECTIONS);
			do {
				PathLegIntersect *pli = pathDest.pAcpi->legIntersects[i];
				if(!intPli && !vec3IsZero(pli->intersect))
					intPli = pli;
				if(pli->leg!=wp->leg)
					legPli = pli;
			} while(i--);
		}


		devassert(legPli && intPli);
		if(intPli->leg == wp->leg)
		{		
			// going from end of one leg to the mid of another leg

			if (! isPointInLegColumn(wp->pos, legPli->leg))
			{	// if we're not already in the dest leg, 
				// make a waypoint that will take us to the edge of the next leg
				AICivilianWaypoint *pNextWp = aiCivilianWaypointAlloc();
				F32 fDistFromLeg;

				pNextWp->acpi = pathDest.pAcpi;
				pNextWp->bReverse = !!randomBool();  // Going from end to mid, can pick direction
				if (pNextWp->bReverse)
					future_forward = !future_forward;

				fDistFromLeg = aiCivPedestrian_GetDesiredDistFromLeg(civ, wp, future_forward);

				civPedestrianGetMidWaypointPos(legPli->leg, intPli, fDistFromLeg, pNextWp->pos);

				aiCiv_AddNewWaypointToPath(&civ->civBase.path, pNextWp);
			}

		}
		else
		{
			// going from the mid of one leg to the end of another
			AICivilianWaypoint *pNextWp;
			bool bReverse;

			{
				int flow = 0;
				stashAddressFindInt(legPli->leg->flowStash, wp->leg, &flow);
				bReverse = (flow != future_forward);
			}

			if (legPli->leg->bIsCrosswalk)
			{
				aiCivPedestrian_ContinueOntoCrosswalk(civ, pathDest.pAcpi, legPli->leg, future_forward, bReverse);

				pNextWp = aiCivilianGetFromLastWaypoint(&civ->civBase, 1);
				if (isPointInLegColumn(pNextWp->pos, wp->leg))
				{
					copyVec3(pNextWp->pos, wp->pos);
				}
				else
				{
					pNextWp = aiCivilianGetFromLastWaypoint(&civ->civBase, 0);
					// we need to move our current waypoint to the intersection of the next leg
					civPedestrianGetMidWaypointPos(wp->leg, intPli, pNextWp->distFromLeg, wp->pos);
				}			
				return true;
			}

			pNextWp = aiCivilianWaypointAlloc();
			pNextWp->acpi = pathDest.pAcpi;
			pNextWp->bReverse = bReverse;
			if(bReverse)
				future_forward = !future_forward;

			aiCivilianMakeWpPos(&civ->civBase, legPli->leg, pNextWp, false);

			// we need to move our current waypoint to the intersection of the next leg
			civPedestrianGetMidWaypointPos(wp->leg, intPli, pNextWp->distFromLeg, wp->pos);

			aiCiv_AddNewWaypointToPath(&civ->civBase.path, pNextWp);
		}

		// create the waypoint that takes us to the end of the next leg
		{
			AICivilianWaypoint *pLegWp = aiCivilianWaypointAlloc();
			pLegWp->bIsLeg = 1;
			pLegWp->leg = legPli->leg;
			aiCivilianMakeWpPos(&civ->civBase, pLegWp->leg, pLegWp, true);
			aiCiv_AddNewWaypointToPath(&civ->civBase.path, pLegWp);
		}

	}

	return true;
}


//-----------------------------------------------------------------------------
// Callout Text
//-----------------------------------------------------------------------------
#define CIVILIAN_CALLOUT_DELAY	10
#define CIVILIAN_PERPLAYER_CALLOUT_DELAY 30

// Returns TRUE if the Civilian can do a Callout for this player
bool aiCivilianCanDoCallout(Entity *e, AICivilian *civ, Entity *pPlayer)
{
	if (civ && entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY) && civ->type == EAICivilianType_PERSON)
	{
		AICivilianPedestrian *pedestrian = (AICivilianPedestrian*)civ;
		U32 uCurTime = timeSecondsSince2000();
		if (pedestrian->calloutInfo.lastCalloutTimestamp < (uCurTime - CIVILIAN_CALLOUT_DELAY))
		{
			int i;
			bool bEmptyHistorySlot = false;
			for (i = 0; i < CALLOUT_HISTORY_SIZE; i++)
			{
				if (pedestrian->calloutHistory[i].timestamp < (uCurTime - CIVILIAN_PERPLAYER_CALLOUT_DELAY))
					bEmptyHistorySlot = true;
				else if (pedestrian->calloutHistory[i].entCalloutEntity == entGetRef(pPlayer))
					return false;
			}
			return bEmptyHistorySlot;
		}
		
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
// Tells the Civilian to say the specified message
void aiCivilianDoCallout(Entity *e, AICivilian *civ, Entity *pPlayer, const char *fsm, const char *messageKey, U64 sourceItem)
{
	PERFINFO_AUTO_START_FUNC();

	if (pPlayer && messageKey && civ && entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY) && civ->type == EAICivilianType_PERSON)
	{
		AICivilianPedestrian *pedestrian = (AICivilianPedestrian*)civ;
		if ((pedestrian->eState == ECivilianState_DEFAULT) && !pedestrian->bIsUsingCrosswalk)
		{
			int i, emptyIndex = -1;

			// Find an empty slot in the Callout History to store this
			for (i = 0; i < CALLOUT_HISTORY_SIZE; i++)
			{
				if (pedestrian->calloutHistory[i].timestamp < (timeSecondsSince2000() - CIVILIAN_PERPLAYER_CALLOUT_DELAY))
				{
					if (emptyIndex == -1)
						emptyIndex = i;
				} 
				else if (pedestrian->calloutHistory[i].entCalloutEntity == entGetRef(pPlayer))
				{
					// Error - this player is already in the Callout History.  Abort
					PERFINFO_AUTO_STOP();
					return;
				}
			}

			if (emptyIndex >= 0 && emptyIndex < CALLOUT_HISTORY_SIZE)
			{
				pedestrian->calloutInfo.messageKey = messageKey;
				pedestrian->calloutInfo.calloutItemID = sourceItem;
				pedestrian->calloutInfo.entCalloutEntity = entGetRef(pPlayer);

				pedestrian->calloutHistory[emptyIndex].timestamp = timeSecondsSince2000();
				pedestrian->calloutHistory[emptyIndex].entCalloutEntity = entGetRef(pPlayer);

				pedestrian->calloutInfo.lastCalloutTimestamp = timeSecondsSince2000();

				aiCivilianRunFSMByName(e, pedestrian, fsm);
			} 
			else 
			{
				// Error - no empty spot in the Callout History array
			}
		}
		
	}

	PERFINFO_AUTO_STOP();
}


// ------------------------------------------------------------------------------------------------------------------

// Only valid for Civilians. Returns the civilian to its normal behavior.
// This should be called when the FSM is finished running.
AUTO_EXPR_FUNC(ai) ACMD_NAME(CalloutFinished);
void exprFuncCalloutFinished(ACMD_EXPR_SELF Entity *e)
{
	AICivCalloutInfo  *calloutInfo = NULL;
	if(!e->pCritter)
		return;

	if (e->pCritter->civInfo)
	{
		AICivilianPedestrian *civ;
		if(e->pCritter->civInfo->type != EAICivilianType_PERSON)
			return;

		civ = (AICivilianPedestrian*)e->pCritter->civInfo;

		calloutInfo = &civ->calloutInfo;

		aiCivPedestrian_StopFSM(e, civ);
	}
	else if (e->aibase && e->aibase->calloutInfo)
	{
		// non-auto generated civilians doing callouts
		AIVarsBase *aib = e->aibase;
		AIJob *job;

		calloutInfo = aib->calloutInfo;
		job = calloutInfo->job;
		devassert(job);

		if (entGetRef(e) == job->assignedBE)
		{
			aiJobUnassign(e, calloutInfo->job);
			entSetRot(e, aib->calloutInfo->lastRot, true, "PostCivFSM");
			entSetFacePY(e, aib->calloutInfo->lastFace, "PostCivFSM");
		}

		eaPush(&g_eaQueuedDestroyJobs, calloutInfo->job);

		calloutInfo->job = NULL;
	}

	if (calloutInfo)
	{
		// clear out any data saved for running the FSM
		calloutInfo->messageKey = NULL;
		calloutInfo->entCalloutEntity = 0;
		calloutInfo->calloutItemID = 0;
		calloutInfo->pszEmoteAnimReaction = NULL;
	}


}

// ------------------------------------------------------------------------------------------------------------------

// Only valid for civilians. Only valid for Item callouts
// Gets the callout message stored on the Civilian. Currently, it is only valid for Item callouts.
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetCalloutMsg);
const char* exprFuncGetCalloutMsg(ACMD_EXPR_SELF Entity *e)
{
	AICivilianPedestrian *civ;
	if(!e || !e->pCritter || !e->pCritter->civInfo || e->pCritter->civInfo->type != EAICivilianType_PERSON)
		return NULL;

	civ = (AICivilianPedestrian*)e->pCritter->civInfo;
	return civ->calloutInfo.messageKey;
}

// ------------------------------------------------------------------------------------------------------------------

AUTO_EXPR_FUNC(ai) ACMD_NAME(GrantCalloutMission);
ExprFuncReturnVal exprGrantCalloutMission(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN ents, const char *pJournalCat, ACMD_EXPR_ERRSTRING errString)
{
	if(!eaSize(ents))
	{
		estrPrintf(errString, "No entities found for GrantCalloutMission.  Requires one target.");
		return ExprFuncReturnError;
	}
	if(eaSize(ents)>1)
	{
		estrPrintf(errString, "Too many entities found for GrantCalloutMission.  Requires only one target.");
		return ExprFuncReturnError;
	}

	mission_OfferRandomAvailableMission((*ents)[0], pJournalCat);
	return ExprFuncReturnFinished;
}

// ------------------------------------------------------------------------------------------------------------------

AUTO_EXPR_FUNC(ai) ACMD_NAME(CheckForGrantingCalloutMission);
int exprCheckForGrantingCalloutMission(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN ents, const char *pJournalCat, ACMD_EXPR_ERRSTRING errString)
{
	if(!eaSize(ents))
	{
		estrPrintf(errString, "No entities found for CheckForCalloutMission.  Requires one target.");
		return ExprFuncReturnError;
	}
	if(eaSize(ents)>1)
	{
		estrPrintf(errString, "Too many entities found for CheckForCalloutMission.  Requires only one target.");
		return ExprFuncReturnError;
	}

	return mission_CanPlayerTakeRandomMission((*ents)[0], pJournalCat);
}

// ------------------------------------------------------------------------------------------------------------------

__forceinline static Entity *aiCivilianGetCalloutTarget(Entity *e)
{
	AICivCalloutInfo *calloutInfo = NULL;

	if (!e->pCritter)
		return NULL;

	if (e->pCritter->civInfo)
	{
		if (e->pCritter->civInfo->type == EAICivilianType_PERSON)
		{
			calloutInfo = &((AICivilianPedestrian*)e->pCritter->civInfo)->calloutInfo;
		}

	}
	else if (e->aibase)
	{
		calloutInfo = e->aibase->calloutInfo;
	}

	if (calloutInfo)
	{
		return entFromEntityRef(entGetPartitionIdx(e), calloutInfo->entCalloutEntity);
	}

	return NULL;
}

// Only valid for civilians. Valid for all callouts: Item callouts, OnClick and Emote reactions
// Gets the target of the callout.
AUTO_EXPR_FUNC(ai) ACMD_NAME(GetCalloutTarget);
void exprFuncGetCalloutTarget(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_OUT ents)
{
	Entity *pTarget = aiCivilianGetCalloutTarget(e);

	eaSetSize(ents, 0);
	if(pTarget && !exprFuncHelperShouldExcludeFromEntArray(pTarget))
		eaPush(ents, pTarget);
}

// ------------------------------------------------------------------------------------------------------------------

// Only valid for civilians. Valid for all callouts: Item callouts, OnClick and Emote reactions
// Checks if the callout target is available (i.e. not interacting and not in combat)
AUTO_EXPR_FUNC(ai) ACMD_NAME(IsCalloutTargetAvailable);
int exprFuncIsCalloutTargetAvailable(ACMD_EXPR_SELF Entity *e)
{
	Entity *pTarget = aiCivilianGetCalloutTarget(e);

	if (pTarget)
	{
		ANALYSIS_ASSUME(pTarget);
		if (pTarget->pChar && !pTarget->pChar->uiTimeCombatExit &&
			!interaction_IsPlayerInteracting(pTarget) && 
			!interaction_IsPlayerInDialog(pTarget))
		{
			return 1;
		}
	}

	return 0;
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(civRunFSM) ACMD_LIST(gEntConCmdList);
void entConCivRunFSM(Entity* e, const char *fsm)
{
	if(!e->pCritter || !e->pCritter->civInfo || e->pCritter->civInfo->type != EAICivilianType_PERSON)
		return;

	aiCivilianRunFSMByName(e, (AICivilianPedestrian*)e->pCritter->civInfo, fsm);
}

void item_RemoveByID(Entity *pEnt, U64 itemID, U32 iCount);

// ------------------------------------------------------------------------------------------------------------------
AUTO_EXPR_FUNC(ai) ACMD_NAME(RemoveCalloutItem);
void exprFuncRemoveCalloutItem(ACMD_EXPR_SELF Entity *e, ACMD_EXPR_ENTARRAY_IN ents)
{
	AICivilianPedestrian *civ;
	if(!e->pCritter || !e->pCritter->civInfo || e->pCritter->civInfo->type != EAICivilianType_PERSON)
		return;

	civ = (AICivilianPedestrian*)e->pCritter->civInfo;

	if(!eaSize(ents))
	{
		return;
	}
	if(eaSize(ents)>1)
	{
		return;
	}
	if (civ->calloutInfo.calloutItemID)
	{
		item_RemoveByID((*ents)[0], civ->calloutInfo.calloutItemID, 1);
	}
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND ACMD_NAME(CivCallout) ACMD_LIST(gEntConCmdList);
void entConCivCallout(Entity *e, const char *target, const char *fsm, const char *msg)
{
	Entity *civ = entGetClientTarget(e, target, NULL);
	if(!civ->pCritter || !civ->pCritter->civInfo || civ->pCritter->civInfo->type != EAICivilianType_PERSON)
		return;

	aiCivilianDoCallout(civ, civ->pCritter->civInfo, e, fsm, msg, 0);
}

// ------------------------------------------------------------------------------------------------------------------
// This puts the civilian on cooldown for a specified player, as if the civilian
// had just said a Callout string for that player
void aiCivilianAddCooldownForPlayer(AICivilian *civ, Entity *pPlayer)
{
	PERFINFO_AUTO_START_FUNC();

	if (civ && civ->type == EAICivilianType_PERSON && pPlayer)
	{
		int i, emptyIndex = -1;
		AICivilianPedestrian *pedestrian = (AICivilianPedestrian*)civ;
		U32 uCurTime = timeSecondsSince2000();

		// Find an empty slot in the Callout History to store this
		for (i = 0; i < CALLOUT_HISTORY_SIZE; i++){
			if (pedestrian->calloutHistory[i].timestamp < (uCurTime - CIVILIAN_PERPLAYER_CALLOUT_DELAY)){
				if (emptyIndex == -1)
					emptyIndex = i;
			} else if (pedestrian->calloutHistory[i].entCalloutEntity == entGetRef(pPlayer)){
				// This player is already in the Callout History.  Update timestamp
				pedestrian->calloutHistory->timestamp = uCurTime;
				PERFINFO_AUTO_STOP();
				return;
			}
		}

		if (emptyIndex >= 0 && emptyIndex < CALLOUT_HISTORY_SIZE){
			pedestrian->calloutHistory[emptyIndex].timestamp = uCurTime;
			pedestrian->calloutHistory[emptyIndex].entCalloutEntity = entGetRef(pPlayer);
		} else {
			// Error - no empty spot in the Callout History array
		}
	}

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
// CALLOUTS
// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool aiCiv_CanRunCallout(AICivilianPedestrian *civ, U32 uCalloutTimeout, bool bAllowScaredCivs)
{
	if (! civ->bIsUsingCrosswalk && (civ->eState == ECivilianState_DEFAULT || (bAllowScaredCivs && civ->eState == ECivilianState_SCARED)))
	{
		return timeSecondsSince2000() - civ->calloutInfo.lastCalloutTimestamp > uCalloutTimeout;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static AICivCalloutInfo* entityAsCiv_GetCalloutInfo(Entity *e)
{
	if (!e->aibase->calloutInfo)
	{
		e->aibase->calloutInfo = calloc(1, sizeof(AICivCalloutInfo));
	}

	return e->aibase->calloutInfo;
}

// ------------------------------------------------------------------------------------------------------------------
static void entityAsCiv_RunCalloutFSM(Entity *e, const char *pszFSMName)
{
	devassert(e && e->aibase && e->aibase->calloutInfo);

	if (pszFSMName)
	{
		static AIJob **s_eaTmpJobs = NULL;
		AICivCalloutInfo *calloutInfo = entityAsCiv_GetCalloutInfo(e);

		entGetRot(e, calloutInfo->lastRot);
		entGetFacePY(e, calloutInfo->lastFace);

		// set the fsm for this occurrence.
		g_calloutJobDescDefault.fsmName = (char*)pszFSMName;
		calloutInfo->job = aiJobAdd(&s_eaTmpJobs, &g_calloutJobDescDefault, entGetPartitionIdx(e));
		aiJobAssign(e, calloutInfo->job);
		eaClear(&s_eaTmpJobs);
	}
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool entityAsCiv_CanRunCallout(Entity *e, U32 uCalloutTimeout)
{
	// checking for non-auto generated civilians reacting

	if (e->aibase && !e->aibase->job)
	{
		AIConfig *pConfig;

		pConfig = aiGetConfig(e, e->aibase);
		if (pConfig && pConfig->doesCivilianBehavior)
		{
			return !e->aibase->calloutInfo || 
				timeSecondsSince2000() - e->aibase->calloutInfo->lastCalloutTimestamp > uCalloutTimeout;
		}
	}


	return false; 
}


// ------------------------------------------------------------------------------------------------------------------
static const char* aiCivOnClick_setup(Entity *targetEntity, Entity *civEntity, 
									  AICivCalloutInfo *calloutInfo, AIPedestrianOnClickDef *pOnClickDefOverride)
{
	Vec3 vTargetPos, vCivPos;
	U32 currentTime = timeSecondsSince2000();

	if ((currentTime - calloutInfo->lastCalloutTimestamp) < CIVILIAN_ONCLICK_DELAY)
		return NULL; // pedestrian did a callout recently

	// get a random FSM to run
	entGetPos(civEntity, vCivPos);
	entGetPos(targetEntity, vTargetPos);

	if (distance3SquaredXZ(vCivPos, vTargetPos) <= SQR(s_fPedestrianOnClickDist))
	{
		const char *fsmName;
		// choose a random FSM
		if (!pOnClickDefOverride)
		{
			pOnClickDefOverride = g_civSharedState.pMapDef->pedestrian.onClick ? 
										g_civSharedState.pMapDef->pedestrian.onClick : g_civSharedState.onclick_def;
		}
		
		fsmName = aiCivOnClickDef_GetRandomFSM(pOnClickDefOverride);
		if(!fsmName)
		{
			return NULL;
		}

		calloutInfo->entCalloutEntity = entGetRef(targetEntity);
		calloutInfo->lastCalloutTimestamp = currentTime;

		return fsmName;
	}

	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivilianOnClick(Entity *civEntity, Entity *targetEntity)
{
	const char *fsmName;


	if (g_disableOnClick)
		return;

	if(!targetEntity || aiCheckIgnoreFlags(targetEntity) || !civEntity || !civEntity->pCritter )
		return;

	if (civEntity->pCritter->civInfo)
	{
		AICivilianPedestrian *civ;
		AIPedestrianOnClickDef *pOnClickOverride = NULL;
		if (!entCheckFlag(civEntity, ENTITYFLAG_CIV_PROCESSING_ONLY) ||
			civEntity->pCritter->civInfo->type != EAICivilianType_PERSON)
			return;

		civ = (AICivilianPedestrian*)civEntity->pCritter->civInfo;

		if ((civ->eState != ECivilianState_DEFAULT && civ->eState != ECivilianState_WANDER) || 
			civ->bIsUsingCrosswalk)
			return; // civilian is currently busy doing something else

		if (civ->civBase.civDef)
		{
			AICivPedestrianDef *pPedDef = (AICivPedestrianDef*)civ->civBase.civDef;
			pOnClickOverride = (eaSize(&pPedDef->onClick.eaOnClickInfo)>0)? &pPedDef->onClick : NULL;
		}
		
		fsmName = aiCivOnClick_setup(targetEntity, civEntity, &civ->calloutInfo, pOnClickOverride);

		if (fsmName)
			aiCivilianRunFSMByName(civEntity, civ, fsmName);
	}
	else if (civEntity->aibase)
	{	// non-auto generated civilian acting like one
		AICivCalloutInfo* calloutInfo;

		if (civEntity->aibase->job)
			return; // this critter already has a job

		calloutInfo = entityAsCiv_GetCalloutInfo(civEntity);

		fsmName = aiCivOnClick_setup(targetEntity, civEntity, calloutInfo, NULL);
		if (fsmName)
			entityAsCiv_RunCalloutFSM(civEntity, fsmName);
	}

}



// ------------------------------------------------------------------------------------------------------------------
typedef struct AICivEmoteReact
{
	const char	*pszFSM;
	const char	*pszAnimList;

} AICivEmoteReact;

bool aiCivEmoteGetReactInfo(const char *pszEmote, AICivEmoteReact *infoOut)
{
	AICivEmoteReactInfo *info = NULL;
	AICivEmoteReactDef *def;

	if (!g_civSharedState.pMapDef || !g_civSharedState.pMapDef->pedestrian.emoteReaction)
		return false;
	
	def = g_civSharedState.pMapDef->pedestrian.emoteReaction;

	devassert(infoOut);
	ZeroStruct(infoOut);

	info = aiCivEmoteReactDef_FindReactInfo(def, pszEmote);
	if (info)
	{
		if (info->pszFsm)
		{
			infoOut->pszFSM = info->pszFsm;
		}

		if (info->eaAnimReactions && eaSize(&info->eaAnimReactions))
		{
			infoOut->pszAnimList = eaGet(&info->eaAnimReactions, randomIntRange(0, eaSize(&info->eaAnimReactions)-1));
		}
	}

	if (! infoOut->pszFSM)
	{
		infoOut->pszFSM = def->pszDefaultFsm;
	}

	if (! infoOut->pszAnimList && eaSize(&def->eaDefaultAnimReactions))
	{
		infoOut->pszAnimList = eaGet(&def->eaDefaultAnimReactions, randomIntRange(0, eaSize(&def->eaDefaultAnimReactions)-1));
	}

	return !!infoOut->pszFSM;
}


// ------------------------------------------------------------------------------------------------------------------

// Only valid for civilians. Only valid for Emote reactions.
// Calls AnimListSet with the determined animation list based on the emote that triggered the civilian into a callout
AUTO_EXPR_FUNC(ai) ACMD_NAME(CalloutAnimListEmoteReact);
void exprFuncCalloutAnimListEmoteReact(ACMD_EXPR_SELF Entity* e, ExprContext* context)
{
	AICivCalloutInfo *calloutInfo = NULL;

	if(!e->pCritter)
		return;

	if (e->pCritter->civInfo)
	{
		calloutInfo = &((AICivilianPedestrian*)e->pCritter->civInfo)->calloutInfo;
	}
	else if (e->aibase)
	{
		calloutInfo = e->aibase->calloutInfo;
	}

	if (calloutInfo && calloutInfo->pszEmoteAnimReaction)
	{
		exprFuncAnimListSet(e, context, calloutInfo->pszEmoteAnimReaction);
	}
}

// ------------------------------------------------------------------------------------------------------------------
static const char* aiCivEmoteReact_setup(Entity *pPlayerEnt, AICivCalloutInfo *callout, const char *pszEmote)
{
	AICivEmoteReact react = {0};

	if (! aiCivEmoteGetReactInfo(pszEmote, &react))
	{
		return NULL;
	}

	callout->entCalloutEntity = entGetRef(pPlayerEnt);
	callout->lastCalloutTimestamp = timeSecondsSince2000();
	callout->pszEmoteAnimReaction = react.pszAnimList;

	return react.pszFSM;
}

// ------------------------------------------------------------------------------------------------------------------
// a list of civilians that are to react to the emote
static void aiCivReactToEmote(Entity *pPlayerEnt, Entity **eaEntitiesToReact, const char *pszEmote)
{

	FOR_EACH_IN_EARRAY(eaEntitiesToReact, Entity, e)
	{
		const char *pszFSMName;

		if (e->pCritter->civInfo)
		{
			AICivilianPedestrian *civ = (AICivilianPedestrian*)e->pCritter->civInfo;

			pszFSMName = aiCivEmoteReact_setup(pPlayerEnt, &civ->calloutInfo, pszEmote);

			if (pszFSMName)
			{
				aiCivilianRunFSMByName(e, civ, pszFSMName);
			}
		}
		else
		{
			// non-auto generated civilians reacting
			AICivCalloutInfo* calloutInfo;

			calloutInfo = entityAsCiv_GetCalloutInfo(e);

			pszFSMName = aiCivEmoteReact_setup(pPlayerEnt, calloutInfo, pszEmote);

			entityAsCiv_RunCalloutFSM(e, pszFSMName);
		}

	}
	FOR_EACH_END
}

// ------------------------------------------------------------------------------------------------------------------
int aiCiv_CanEntityDoEmote(Entity *e)
{
	if (! e->pCritter) 
		return false;

	if (e->pCritter->civInfo)
	{
		AICivilianPedestrian *civ;
		if (e->pCritter->civInfo->type != EAICivilianType_PERSON)
			return false;

		civ = (AICivilianPedestrian*)e->pCritter->civInfo;
		if (! aiCiv_CanRunCallout(civ, CIVILIAN_EMOTE_DELAY, false))
			return false;
	}
	else 
	{
		if (! entityAsCiv_CanRunCallout(e, CIVILIAN_EMOTE_DELAY))
			return false;
	}

	return true;
}


// ------------------------------------------------------------------------------------------------------------------
static void aiCivProcessPlayerEmote(const AICivEmoteReactDef *pEmoteDef, Entity *pPlayerEnt, const char *pszEmote)
{
	if (pPlayerEnt && pPlayerEnt->pPlayer)
	{
		static Entity **s_eaCivEmoteReact = NULL;

		if (pEmoteDef->bUseSelectedCivOnly)
		{
			Entity *e = NULL;

			if (!pPlayerEnt->pChar || 
				(!pPlayerEnt->pChar->currentTargetRef && !pPlayerEnt->pChar->erTargetDual))
				return;

			if (pPlayerEnt->pChar->erTargetDual)
				e = entFromEntityRef(entGetPartitionIdx(pPlayerEnt), pPlayerEnt->pChar->erTargetDual);
			else if (pPlayerEnt->pChar->currentTargetRef)
				e = entFromEntityRef(entGetPartitionIdx(pPlayerEnt), pPlayerEnt->pChar->currentTargetRef);
			
			if (!e || !aiCiv_CanEntityDoEmote(e))
				return;
			
			if (entGetDistance(pPlayerEnt, NULL, e, NULL, NULL) > pEmoteDef->fRadius)
				return;
			
			eaPush(&s_eaCivEmoteReact, e);
		}
		else
		{
			S32 i;
			F32 fMaxRadSQR = SQR(pEmoteDef->fRadius);
			aiUpdateProxEnts(pPlayerEnt, pPlayerEnt->aibase);

			// todo: sort by heuristic of selected, and angle distance to center.

			for(i = 0; i < pPlayerEnt->aibase->proxEntsCount; i++)
			{
				EntAndDist *proxEnt = &pPlayerEnt->aibase->proxEnts[i];
				
				if (proxEnt->maxDistSQR > fMaxRadSQR)
					break; // this is a sorted list, so once we find something that is too far, break out
								
				if (!aiCiv_CanEntityDoEmote(proxEnt->e))
					continue;
				
				eaPush(&s_eaCivEmoteReact, proxEnt->e);
				if (eaSize(&s_eaCivEmoteReact) >= pEmoteDef->iMaxCivsToReact)
					break;
			}
		}

		aiCivReactToEmote(pPlayerEnt, s_eaCivEmoteReact, pszEmote);
		eaClear(&s_eaCivEmoteReact);
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivReceivedEmoteEvent(void* structPtr, GameEvent *ev, GameEvent *specific, int value)
{
	if (g_civSharedState.pMapDef && g_civSharedState.pMapDef->pedestrian.emoteReaction)
	{
		if (eaSize(&specific->eaSources) > 0 && specific->pchEmoteName)
		{
			Entity *pEnt;
			GameEventParticipant *geParticipant = specific->eaSources[0];
			const AICivEmoteReactDef *pEmoteDef = g_civSharedState.pMapDef->pedestrian.emoteReaction;

			pEnt = entFromEntityRefAnyPartition(geParticipant->entRef);
			if (pEnt)
			{
				aiCivProcessPlayerEmote(pEmoteDef, pEnt, specific->pchEmoteName);
			}
		}
	}
}




// ------------------------------------------------------------------------------------------------------------------
// Kill responding
// ------------------------------------------------------------------------------------------------------------------


#define KILL_EVENT_UPDATE_TIME	1.0f
#define KILL_EVENT_MAX_TIME		6.0f
#define KILL_EVENT_CALLOUT_TIMEOUT	15
#define KILL_EVENT_MAX_DISTSQ	SQR(35.0f)

// ------------------------------------------------------------------------------------------------------------------
AICivPlayerKillEventManager* aiCivPlayerKillEvents_Create(int partitionIdx)
{
	AICivPlayerKillEventManager *manager;

	if (! g_civSharedState.pMapDef)
	{
		return NULL;
	}

	MP_CREATE(AICivPlayerKillEvent, 100);

	manager = calloc(1, sizeof(AICivPlayerKillEventManager));
	manager->stshPlayerKillEvents = stashTableCreateInt(50);
	manager->partitionIdx = partitionIdx;
	return manager;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPlayerKillEvent_Free(AICivPlayerKillEvent *pEvent)
{
	MP_FREE(AICivPlayerKillEvent, pEvent);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivPlayerKillEvents_Destroy(AICivPlayerKillEventManager **ppManager)
{
	if (*ppManager)
	{
		AICivPlayerKillEventManager *pManager = *ppManager;

		if (pManager->stshPlayerKillEvents)
		{
			stashTableDestroyEx(pManager->stshPlayerKillEvents, NULL, aiCivPlayerKillEvent_Free);
		}

		eaDestroy(&pManager->eaEventsToRemove);

		free(*ppManager);
		*ppManager = NULL;
	}

}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivPlayerKillEvents_GatherExpired(AICivPlayerKillEventManager *pManager, StashElement element)
{
	AICivPlayerKillEvent *killEvent = stashElementGetPointer(element);

	if (killEvent->bCompleted || 
		(ABS_TIME_SINCE_PARTITION(pManager->partitionIdx, killEvent->iKillReportTime) >= SEC_TO_ABS_TIME(KILL_EVENT_MAX_TIME)))
	{
		eaPush(&pManager->eaEventsToRemove, killEvent);
	}

	return 1;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivPlayerKillEvent_SetToExpire(AICivPlayerKillEvent *pEvent)
{
	// sets an event to expire on the next update
	pEvent->bCompleted = true;
}


static const char* aiCivPlayerKillEvent_setup(Entity *targetPlayer, AICivCalloutInfo *calloutInfo)
{	
	calloutInfo->entCalloutEntity = entGetRef(targetPlayer);
	calloutInfo->lastCalloutTimestamp = timeSecondsSince2000();

	return g_civSharedState.pMapDef->pedestrian.pchPlayerKillCritterFSM;
}

// ------------------------------------------------------------------------------------------------------------------
static bool aiCivPlayerKillEvent_CivilianCheerPlayer(Entity *civEntity, Entity *targetPlayer)
{
	const char *pszFSMName = NULL;

	if (civEntity->pCritter->civInfo)
	{
		AICivilianPedestrian *civ = getAICivilianAsPed(civEntity->pCritter->civInfo);

		if (! civ)
			return false;

		pszFSMName = aiCivPlayerKillEvent_setup(targetPlayer, &civ->calloutInfo);
		if (pszFSMName)
		{
			aiCivilianRunFSMByName(civEntity, civ, pszFSMName);
			return true;
		}
	}
	else
	{
		AICivCalloutInfo *calloutInfo = entityAsCiv_GetCalloutInfo(civEntity);

		pszFSMName = aiCivPlayerKillEvent_setup(targetPlayer, calloutInfo);
		if (pszFSMName)
		{
			entityAsCiv_RunCalloutFSM(civEntity, pszFSMName);
			return true;
		}
	}


	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivPlayerKillEvent_Update(StashElement element)
{
	AICivPlayerKillEvent *pEvent = stashElementGetPointer(element);
	Entity *pPlayerEnt = entFromEntityRefAnyPartition(pEvent->playerEntRef);
	S32 i;
	AIVarsBase *aib;

	if ( !pPlayerEnt || !pPlayerEnt->aibase || !pPlayerEnt->pPlayer)
	{	// the player entity is no longer around, set this event to expire next time we update
		aiCivPlayerKillEvent_SetToExpire(pEvent);
		return true;
	}
	aib = pPlayerEnt->aibase;

	aiUpdateProxEnts(pPlayerEnt, aib);

	for(i = 0; i < aib->proxEntsCount; i++)
	{
		EntAndDist *proxEnt = &aib->proxEnts[i];

		if (proxEnt->maxDistSQR > KILL_EVENT_MAX_DISTSQ)
			break; // this is a sorted list, so once we find something that is too far, break out

		if (entCheckFlag(proxEnt->e, ENTITYFLAG_CIVILIAN))
		{
			

			if (!proxEnt->e->pCritter || !proxEnt->e->pCritter->civInfo )
			{
				AICivilianPedestrian *ped = getAICivilianAsPed(proxEnt->e->pCritter->civInfo);

				if (!ped || !aiCiv_CanRunCallout(ped, CIVILIAN_KILL_CHEER_DELAY, true))
				continue;
			}
		}
		else 
		{	// checking for non-auto generated civilians reacting

			if (! entityAsCiv_CanRunCallout(proxEnt->e, CIVILIAN_KILL_CHEER_DELAY))
				continue;
		}

		// try and create get this Entity to cheer the player
		// 

		if (aiCivPlayerKillEvent_CivilianCheerPlayer(proxEnt->e, pPlayerEnt))
		{
			// found a civilian to respond
			aiCivPlayerKillEvent_SetToExpire(pEvent);
			pPlayerEnt->pPlayer->lastKillCalloutTime = timeSecondsSince2000();
			break;
		}

	}

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivPlayerKillEvents_Update(AICivPlayerKillEventManager *pManager)
{
	if (pManager)
	{
		PERFINFO_AUTO_START_FUNC();

		if (ABS_TIME_SINCE_PARTITION(pManager->partitionIdx, pManager->iLastUpdateTime) > SEC_TO_ABS_TIME(KILL_EVENT_UPDATE_TIME))
		{
			pManager->iLastUpdateTime = ABS_TIME_PARTITION(pManager->partitionIdx);

			// destroy all expired events
			{
				S32 i;

				stashForEachElementEx(pManager->stshPlayerKillEvents, aiCivPlayerKillEvents_GatherExpired, pManager);
				for (i = 0; i < eaSize(&pManager->eaEventsToRemove); i++)
				{
					AICivPlayerKillEvent *killEvent = pManager->eaEventsToRemove[i];

					stashIntRemovePointer(pManager->stshPlayerKillEvents, killEvent->playerEntRef, NULL);

					aiCivPlayerKillEvent_Free(killEvent);
				}

				eaClear(&pManager->eaEventsToRemove);
			}

			// update all the active events					
			stashForEachElement(pManager->stshPlayerKillEvents, aiCivPlayerKillEvent_Update);
		}

		PERFINFO_AUTO_STOP();
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianReportPlayerKillEvent(Entity *pPlayerEnt)
{
	AICivPlayerKillEvent *pKillEvent = NULL;
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(pPlayerEnt));
	AICivPlayerKillEventManager *pKillEventManager;

	if (!pPartition)
		return;
	
	pKillEventManager = pPartition->pKillEventManager;

	if (!pKillEventManager || ! g_civSharedState.pMapDef || !g_civSharedState.pMapDef->pedestrian.pchPlayerKillCritterFSM)
		return;
	if (!pPlayerEnt || !pPlayerEnt->aibase || !pPlayerEnt->pPlayer)
		return;


	// check if we have an active kill event, if we do, update it if it's not completed
	if (stashIntFindPointer(pKillEventManager->stshPlayerKillEvents, pPlayerEnt->myRef, &pKillEvent))
	{
		if (!pKillEvent->bCompleted)
		{
			// reset the time
			pKillEvent->iKillReportTime = ABS_TIME_PARTITION(pPartition->iPartitionIndex);
		}
		return;
	}

	if (timeSecondsSince2000() - pPlayerEnt->pPlayer->lastKillCalloutTime < KILL_EVENT_CALLOUT_TIMEOUT)
	{
		// player had a callout earlier, ignore this one
		return;
	}

	pKillEvent = MP_ALLOC(AICivPlayerKillEvent);
	pKillEvent->iKillReportTime = ABS_TIME_PARTITION(pPartition->iPartitionIndex);
	pKillEvent->playerEntRef = pPlayerEnt->myRef;

	stashIntAddPointer(pKillEventManager->stshPlayerKillEvents, pKillEvent->playerEntRef, pKillEvent, 0);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilianUpdateIsHostile(Entity *pEnt)
{
	if(pEnt && pEnt->aibase)
	{
		const char *pszCivilianFaction = aiCivGetPedestrianFaction();
		if (pszCivilianFaction)
		{
			CritterFaction *faction = entGetFaction(pEnt);
			if (faction)
			{
				EntityRelation relation = faction_GetRelation(critter_FactionGetByName(pszCivilianFaction), faction);
				pEnt->aibase->isHostileToCivilians = (relation==kEntityRelation_Foe);
			}
		}
	}
}



// ------------------------------------------------------------------------------------------------------------------
// Points of interest / Civ Interaction Points
// ------------------------------------------------------------------------------------------------------------------

static bool aiCivPOI_Update(S32 iPartitionIdx, AICivPOI *pCivPOI);

// ------------------------------------------------------------------------------------------------------------------
__forceinline static AICivPOIUser* aiCivPOIUser_Alloc()
{
	return MP_ALLOC(AICivPOIUser);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivPOIUser_Free(AICivPOIUser *pAICivPOI)
{
	MP_FREE(AICivPOIUser, pAICivPOI);
}

// ------------------------------------------------------------------------------------------------------------------
AICivPOIManager* aiCivPOIManager_Create()
{
	AICivPOIManager *pPOIMan = calloc(1, sizeof(AICivPOIManager));
	
	MP_CREATE(AICivPOIUser, 100);
	
	s_globalPOIConfig.fPOICalloutChance = 0.25f;

	return pPOIMan;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivPOIManager_Destroy(AICivPOIManager **ppManager)
{
	if (*ppManager)
	{
		AICivPOIManager *pManager = *ppManager;

		eaDestroyEx(&pManager->eaCivPOI, NULL);
		eaDestroy(&pManager->eaTmpSearchEnts);

		free(pManager);
		*ppManager = NULL;
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivPOIManager_AddPOI(S32 iPartitionIdx, AICivPOIManager *pManager, GameInteractable *poi)
{
	if (eaSize(&poi->eaInteractLocations) > 0)
	{
		AICivPOI *civPoi = calloc(1, sizeof(AICivPOI));
		civPoi->worldPOI = poi;
		civPoi->iPartitionIdx = iPartitionIdx;
		eaPush(&pManager->eaCivPOI, civPoi);
	}

}

// ------------------------------------------------------------------------------------------------------------------
void aiCivPOIManager_Update(AICivPOIManager *pManager)
{
	if (pManager && eaSize(&pManager->eaCivPOI))
	{
		pManager->iCurUpdateIdx = eaForEachPartial(0, pManager->eaCivPOI, pManager->iCurUpdateIdx, 5, aiCivPOI_Update);
	}	
}


// ------------------------------------------------------------------------------------------------------------------
static S32 aiCivPOI_GetNumSpots(AICivPOI *pCivPOI)
{
	return eaSize(&pCivPOI->worldPOI->eaInteractLocations);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPOI_SetUsed(AICivPOI *pCivPOI, AICivPOIUser *pUser)
{
	GameInteractLocation *pLoc = eaGet(&pCivPOI->worldPOI->eaInteractLocations, pUser->iPOISlot);

	if (pLoc)
	{
		GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(pCivPOI->iPartitionIdx, pLoc);
		eaPush(&pCivPOI->eaUsers, pUser);
		locPart->bOccupied = true;
		locPart->bReachedLocation = false;
	}
}

// ------------------------------------------------------------------------------------------------------------------
GameInteractable* aiCivPedestrian_GetCurrentAmbientJob(Entity *e)
{
	AICivilianPedestrian *civ;
	if(!e->pCritter || !e->pCritter->civInfo || e->pCritter->civInfo->type != EAICivilianType_PERSON)
		return 0;

	civ = (AICivilianPedestrian*)e->pCritter->civInfo;
	if(civ->pPOIUser && civ->pPOIUser->civPoi)
	{
		return civ->pPOIUser->civPoi->worldPOI;
	}

	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
static void aiCivPOI_StopUsing(int iPartitionIdx, AICivPOI *pCivPOI, AICivPOIUser *pUser)
{
	S32 idx;
	GameInteractLocation *pLoc = eaGet(&pCivPOI->worldPOI->eaInteractLocations, pUser->iPOISlot);
	const AICivPedestrianPOIParams* pPOIParams = aiCivPedestrian_GetPOIParams();

	aiAmbientJob_CheckForOccupiedApplyCooldown(iPartitionIdx, pCivPOI->worldPOI, pPOIParams->fAmbientJobCooldownTime);

	if (pLoc)
	{
		GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(iPartitionIdx, pLoc);
		locPart->bOccupied = false;
	}

	idx = eaFindAndRemoveFast(&pCivPOI->eaUsers, pUser);
	devassert(idx != -1);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivPOI_GetPosRot(AICivPOI *pCivPOI, const AICivPOIUser *pUser, Vec3 vOutPos, F32 *pfOutRot )
{
	GameInteractLocation *pLoc = eaGet(&pCivPOI->worldPOI->eaInteractLocations, pUser->iPOISlot);
	if (pLoc)
	{
		Vec3 pyr;
		
		quatToPYR( pLoc->pWorldInteractLocationProperties->qOrientation, pyr);
		
		copyVec3(pLoc->pWorldInteractLocationProperties->vPos, vOutPos);
		*pfOutRot = pyr[1];
	}
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCivPOI_SetReachedAndGetAction(AICivPOI *pCivPOI, S32 iSlot, FSM **ppFSM, AIAnimList **ppAnimList)
{
	GameInteractLocation *pLoc = eaGet(&pCivPOI->worldPOI->eaInteractLocations, iSlot);
	if (pLoc)
	{
		GameInteractLocationPartition *locPart = interactable_GetInteractLocationPartition(pCivPOI->iPartitionIdx, pLoc);
		locPart->bReachedLocation = true;
		return aiAmbient_GetAction(pLoc, ppFSM, ppAnimList);
	}

	return false;
}

// ---------------------------------------------------------------------------------------
static F32 aiCivPOI_GetAnimDuration(AICivPOI *pCivPOI, S32 iSlot, const Vec3 vPos)
{
	if (g_civSharedState.pMapDef->pedestrian.POIParams.fAmbientAnimDuration > 0)
		return g_civSharedState.pMapDef->pedestrian.POIParams.fAmbientAnimDuration;
	else
	{
		AIAmbient* pAIAmbientDef = aiAmbient_GetAmbientDef(vPos);
		if (pAIAmbientDef)
		{
			return pAIAmbientDef->fJobDuration;
		}
	}
	// return some default
	return 10.f;
}


// ------------------------------------------------------------------------------------------------------------------
static bool aiCivPOI_Update(S32 iUnused, AICivPOI *pCivPOI)
{
	S32 numSlots = aiCivPOI_GetNumSpots(pCivPOI);
	S32 numUsers = eaSize(&pCivPOI->eaUsers);
	F32 fCalloutChance;
	int iPartitionIdx = pCivPOI->iPartitionIdx;
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	AICivPOIManager *pPOIManager;
	devassert(pPartition);
	
	pPOIManager = pPartition->pPOIManager;

	// check if the interactable is hidden
	if (interactable_IsHiddenOrDisabled(iPartitionIdx, pCivPOI->worldPOI))
		return false; 
		
	if (pPartition->iNormalCivilianCount >= CIV_NORMAL_COUNT_THRESHOLD)
		return false;
	if (numUsers >= numSlots)
	{
		return false; 
	}

	if (interactable_GetAmbientCooldownTime(iPartitionIdx, pCivPOI->worldPOI) > ABS_TIME_PARTITION(iPartitionIdx))
	{	// POI is on cooldown
		return false; 
	}

	if (numUsers == 0)
	{	// no users
		fCalloutChance = s_globalPOIConfig.fPOICalloutChance;
	}
	else 
	{
		fCalloutChance = 0.5f;
	}

	
	if (randomChance(fCalloutChance))
	{
		const AICivPedestrianPOIParams* pPOIParams;

		// we have an available slot and passed a chance check.
		// callout to any nearby civilians to come and use this POI
		S32 i;
		Vec3 vPOIPos;
		S32 iFreeSlot = -1;
		GameInteractLocation *pAmbientJobLoc;
	
		pPOIParams = aiCivPedestrian_GetPOIParams();
		
		if (aiAmbientChooseAvailableJob(pCivPOI->iPartitionIdx,
										pCivPOI->worldPOI, 
										NULL, 
										NULL, &iFreeSlot))
		{
			devassert(iFreeSlot != -1);
			pAmbientJobLoc = pCivPOI->worldPOI->eaInteractLocations[iFreeSlot];
			copyVec3(pAmbientJobLoc->pWorldInteractLocationProperties->vPos, vPOIPos);
		}
		else
		{
			return false;
		}
		
		
		eaClear(&pPOIManager->eaTmpSearchEnts);

		vPOIPos[1] += 3.f;

		// find all nearby pedestrians 
		entGridProximityLookupExEArray(iPartitionIdx, vPOIPos, &pPOIManager->eaTmpSearchEnts, 
										pPOIParams->fAmbientJobCalloutRange, 
										ENTITYFLAG_CIV_PROCESSING_ONLY, 0, NULL);

		
		for (i = 0; i < eaSize(&pPOIManager->eaTmpSearchEnts); i++)
		{
			Entity *ent = pPOIManager->eaTmpSearchEnts[i];

			assert (entCheckFlag(ent, ENTITYFLAG_CIV_PROCESSING_ONLY));

			if(!ent->pCritter || !ent->pCritter->civInfo || ent->pCritter->civInfo->type != EAICivilianType_PERSON)
			{
				continue;
			}

			if (aiCiv_CanUsePOI((AICivilianPedestrian*) ent->pCritter->civInfo))
			{
				if(aiAmbient_ShouldEntityIgnoreJob(ent, pCivPOI->worldPOI, pAmbientJobLoc))
					continue;

				// check if we have LOS to the POI
				if (pPOIParams->bAmbientJobCheckLineOfSight)
				{	// 
					Vec3 vCivPos, vPOIPosTmp;
					WorldCollCollideResults results = {0};
					
					entGetPos(ent, vCivPos);
					vCivPos[1] += 3.f;
					copyVec3(vPOIPos, vPOIPosTmp);
					vPOIPosTmp[1] += 3.f;
					
					if(worldCollideRay(iPartitionIdx, vPOIPosTmp, vCivPos, WC_QUERY_BITS_AI_CIV, &results))
					{	// hit something
						continue;	
					}
				}

				aiCiv_UsePOI(ent, (AICivilianPedestrian*)ent->pCritter->civInfo, iFreeSlot, pCivPOI);
				break;
			}
		}

		eaClear(&pPOIManager->eaTmpSearchEnts);
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivilian_ReleaseAllPOIUsers(AICivilianPartitionState *pPartition)
{
	S32 i;
	
	for(i = 0; i < eaSize(&pPartition->civilians[0]); i++)
	{
		AICivilianPedestrian *pPedestrian = (AICivilianPedestrian*)pPartition->civilians[0][i];

		if (pPedestrian->pPOIUser)
		{
			Entity *ent;
			aiCivPOIUser_Free(pPedestrian->pPOIUser);
			pPedestrian->pPOIUser = NULL;

			if (!g_civSharedState.clearingAllData)
			{
				ent = entFromEntityRefAnyPartition(pPedestrian->civBase.myEntRef);
				if (ent)
				{
					aiCivPedestrian_StopFSMInternal(ent, pPedestrian);
				}
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCiv_CanUsePOI(AICivilianPedestrian *civ)
{
	if ( (civ->eState == ECivilianState_DEFAULT && ! civ->bIsUsingCrosswalk) ||
		 (civ->eState == ECivilianState_WANDER) )
	{
		AICivilianWaypoint * curWp = aiCivilianGetCurrentWaypoint(&civ->civBase);
		// make sure we are not waiting for a crosswalk
		if (curWp && !(curWp->bStop || curWp->pCrosswalkUser))
		{	
			const U32 poiUseTimeout = 20;
			U32 curTime = timeSecondsSince2000();
			return (curTime - civ->lastPOITime > poiUseTimeout);
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCiv_UsePOI(Entity *e, AICivilianPedestrian *civ, S32 iPOISlot, AICivPOI *pCivPOI)
{
	AICivPOIUser *pUser = aiCivPOIUser_Alloc();

	// set up the AICivPOIUser user
	pUser->civ = civ;
	pUser->civPoi = pCivPOI;
	pUser->iPOISlot = iPOISlot;

	// set the user on the AICivilian and AICivPOI
	civ->pPOIUser = pUser;

	aiCivPOI_SetUsed(pCivPOI, pUser);
	

	aiCivPedestrian_SetState(civ, ECivilianState_POI);

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
static int aiCiv_IsUsingPOI(AICivilianPedestrian *ped)
{
	return ped->pPOIUser != NULL;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCiv_CleanupPOI(int iPartitionIdx, AICivilianPedestrian *civ)
{
	if (civ->pPOIUser)
	{
		AICivPOI *pCivPOI = civ->pPOIUser->civPoi;

		aiCivPOI_StopUsing(iPartitionIdx, pCivPOI, civ->pPOIUser);
		
		aiCivPOIUser_Free(civ->pPOIUser);
		civ->pPOIUser = NULL;
	}
}


// ------------------------------------------------------------------------------------------------------------------
static void addClientAxisLines(Entity *ent, Vec3 vPos, Mat3 mtx)
{
	Vec3 vPt;
	scaleAddVec3(mtx[0], 5.f, vPos, vPt);
	wlAddClientLine(ent, vPos, vPt, 0xFFFF0000);
	scaleAddVec3(mtx[1], 5.f, vPos, vPt);
	wlAddClientLine(ent, vPos, vPt, 0xFF00FF00);
	scaleAddVec3(mtx[2], 5.f, vPos, vPt);
	wlAddClientLine(ent, vPos, vPt, 0xFF0000FF);
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiCivSendPOI(Entity *e)
{
	AICivilianPartitionState *pPartition;


	if (!e) 
	{
		e = entFromEntityRefAnyPartition(aiCivDebugRef);
		if (!e) return;
	}
	
	pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e));

	if (pPartition && pPartition->pPOIManager)
	{
		AICivPOIManager *pPOIManager = pPartition->pPOIManager;

		FOR_EACH_IN_EARRAY(pPOIManager->eaCivPOI, AICivPOI, pPOI)
		{
			if (pPOI->worldPOI)
			{
				Mat3 mtxPOI;
				
				FOR_EACH_IN_EARRAY(pPOI->worldPOI->eaInteractLocations, GameInteractLocation, pLoc)
				{
					Vec3 vChildPos;

					quatToMat(pLoc->pWorldInteractLocationProperties->qOrientation, mtxPOI);
					copyVec3(pLoc->pWorldInteractLocationProperties->vPos, vChildPos);
					vChildPos[1] += 3.f;
					addClientAxisLines(e, vChildPos, mtxPOI);

				}
				FOR_EACH_END
			}
		}
		FOR_EACH_END
		
	}
}

// ---------------------------------------------------------------
// Civilian Wander Area
// ---------------------------------------------------------------


static void aiCivWanderArea_GetValidBeacons(int partitionIdx, AICivWanderArea *pWanderArea);

// -------------------------------------------------------------------------------------------------------
void aiCivWanderArea_Free(AICivWanderArea *pWanderArea)
{
	// go through all the peds and tell them that the area is being destroyed	
	eaDestroy(&pWanderArea->eaCivilians);
	eaDestroy(&pWanderArea->eaStartBeacons);
	free(pWanderArea);
}

// -------------------------------------------------------------------------------------
void aiCivWanderArea_Update(AICivilianPartitionState *pPartition)
{
	S32 i;
	
	for(i = eaSize(&pPartition->eaWanderAreas) - 1; i >= 0; --i)
	{
		AICivWanderArea *pWanderArea = pPartition->eaWanderAreas[i];

		if (!pWanderArea->eaStartBeacons)
		{
			aiCivWanderArea_GetValidBeacons(pPartition->iPartitionIndex, pWanderArea);
			if (eaSize(&pWanderArea->eaStartBeacons) == 0)
			{	// could not find any start beacons 
				aiCivWanderArea_Free(pWanderArea);
				eaRemoveFast(&pPartition->eaWanderAreas, i);
			}
			// only do one a frame
			break;
		}
	}
}

// -------------------------------------------------------------------------------------
// returns the index of the beacon connection that the given beacon is connected to pConnecetedBeacon
// returns -1 if no connection exists
__forceinline static int findIndexBeaconConnectedToBeaconViaGround(const Beacon* beacon, const Beacon* pConnecetedBeacon)
{
	int x;
	assert(pConnecetedBeacon);
	for (x = 0; x < pConnecetedBeacon->gbConns.size; x++)
	{
		BeaconConnection *pConnection = pConnecetedBeacon->gbConns.storage[x];
		if (pConnection->destBeacon == beacon)
			return x;
	}

	return -1;
}

__forceinline static bool hasMinimumIncomingGroundConnections(const Beacon* beacon)
{
	int iIncomingGroundConnections = 0;
	int i;

	// check all our ground connections 
	for (i = 0; i < beacon->gbConns.size; i++)
	{
		BeaconConnection *pConnection = beacon->gbConns.storage[i];

		if (findIndexBeaconConnectedToBeaconViaGround(beacon, pConnection->destBeacon) != -1)
		{
			if (++iIncomingGroundConnections >= 3 )
				return true;
		}
	}

	return false;
}

typedef struct FilterBeaconsData {
	AICivWanderArea *area;
	int partitionIdx;
} FilterBeaconsData; 

// -------------------------------------------------------------------------------------------------------
static bool filterStartBeacons(Beacon* beacon, FilterBeaconsData* pParams)
{
	// check to see if this beacon has enough ground connections
	// 
	AICivWanderArea *pArea = pParams->area;

	if (beacon->gbConns.size >= 3)
	{
		if (hasMinimumIncomingGroundConnections(beacon))
		{
			if (aiCivWanderArea_IsPosInArea(pArea, beacon->pos))
			{
				BeaconPartitionData *beaconPartition = beaconGetPartitionData(beacon, pParams->partitionIdx, false);

				if (beaconPartition && beaconPartition->block && beaconPartition->block->galaxy)
				{
					BeaconBlock *pGalaxy = beaconPartition->block->galaxy;
					S32 i, beaconCount = 0;

					for (i = 0; i < pGalaxy->subBlockArray.size; i++)
					{
						BeaconBlock *pBlock = pGalaxy->subBlockArray.storage[i];
						beaconCount += pBlock->beaconArray.size;
					}

					return (beaconCount > 5);
				}
				else
				{
					return true;
				}
			}
		}
	}
	return false;
}

// -------------------------------------------------------------------------------------------------------
static void aiCivWanderArea_GetValidBeacons(int partitionIdx, AICivWanderArea *pWanderArea)
{
	FilterBeaconsData data;
	eaClear(&pWanderArea->eaStartBeacons);

	data.area = pWanderArea;
	data.partitionIdx = partitionIdx;

	beaconGetNearbyBeacons(&pWanderArea->eaStartBeacons, 
							pWanderArea->vWorldMid, pWanderArea->fRadius, 
							(FilterBeaconFunc)filterStartBeacons, (void*)&data);
}


// -------------------------------------------------------------------------------------------------------
void aiCivWanderArea_AddVolume(AICivilianPartitionState *pPartition, WorldVolumeEntry *pEntry)
{
	Vec3 vWorldMin, vWorldMax, vExtents;
	AICivWanderArea *pWanderArea;

	// make sure we don't add duplicates
	FOR_EACH_IN_EARRAY(pPartition->eaWanderAreas, AICivWanderArea, pWander)
	{
		if (pWander->pVolumeEntry == pEntry)
			return;
	}
	FOR_EACH_END

	{
		// make sure the volume isn't too big, if so throw an error and ignore this volume
		F32 width, depth;
		
		width = pEntry->base_entry.shared_bounds->local_max[0] - pEntry->base_entry.shared_bounds->local_min[0];
		depth = pEntry->base_entry.shared_bounds->local_max[2] - pEntry->base_entry.shared_bounds->local_min[2];

		if (width * depth > SQR(152.f))
		{
			Errorf("Civilian wander volume is too large. Max size is 150x150 (Height ignored.) Ignoring volume for wandering. ");
			return;
		}
	}
	
	pWanderArea = calloc(1, sizeof(AICivWanderArea));
	pWanderArea->pVolumeEntry = pEntry;

	
	{
		copyVec3(pEntry->base_entry.shared_bounds->local_min, pWanderArea->vLocalMin);
		copyVec3(pEntry->base_entry.shared_bounds->local_max, pWanderArea->vLocalMax);
		mat3ToQuat(pEntry->base_entry.bounds.world_matrix, pWanderArea->qInvRot);
		quatInverse(pWanderArea->qInvRot, pWanderArea->qInvRot);

		copyVec3(pEntry->base_entry.bounds.world_matrix[3], pWanderArea->vMtxPos);
	}

	{
		mulVecMat4(pEntry->base_entry.shared_bounds->local_min, pEntry->base_entry.bounds.world_matrix, vWorldMin);
		mulVecMat4(pEntry->base_entry.shared_bounds->local_max, pEntry->base_entry.bounds.world_matrix, vWorldMax);
		interpVec3(0.5f, vWorldMin, vWorldMax, pWanderArea->vWorldMid);
		pWanderArea->fRadius = distance3(vWorldMax, pWanderArea->vWorldMid);
	}

	subVec3(vWorldMax, vWorldMin, vExtents);

	pWanderArea->maxWanderers = (S32)ceil( (vExtents[0] * vExtents[1]) / SQR(13.f));
		 
	eaPush(&pPartition->eaWanderAreas, pWanderArea);
}

// -------------------------------------------------------------------------------------------------------
void aiCivWanderArea_RemoveVolume(AICivilianPartitionState *pPartition, WorldVolumeEntry *pEntry)
{
	S32 i, count = eaSize(&pPartition->eaWanderAreas);
	S32 remIdx = -1;
	AICivWanderArea *pWanderArea = NULL;
	for (i = 0; i < count; ++i)
	{	
		pWanderArea = pPartition->eaWanderAreas[i];
		if (pWanderArea->pVolumeEntry == pEntry)
		{
			remIdx = i;
			break;
		}
	}

	if (remIdx == -1)
		return; // could not find

	// go through all the civilians that are using the wander and clear their wander volume
	FOR_EACH_IN_EARRAY(pWanderArea->eaCivilians, AICivilianPedestrian, pPed)
		pPed->pWanderArea = NULL;
	FOR_EACH_END
	
	eaRemoveFast(&pPartition->eaWanderAreas, remIdx);
	aiCivWanderArea_Free(pWanderArea);
}


// ------------------------------------------------------------------------------------------------------------------
static void aiCivWanderArea_AddVolumeWrapper(int iPartitionIdx, void *p)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	WorldVolumeEntry *pEntry = (WorldVolumeEntry*)p;
	if (pPartition)
	{
		aiCivWanderArea_AddVolume(pPartition, pEntry);
	}
}
// adds the given volume to every partition
void aiCivWanderArea_AddVolumeToEachPartition(WorldVolumeEntry *pEntry)
{
	partition_ExecuteOnEachPartitionWithData(aiCivWanderArea_AddVolumeWrapper, pEntry);
}



// ------------------------------------------------------------------------------------------------------------------
static void aiCivWanderArea_RemoveVolumeWrapper(int iPartitionIdx, void *p)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	WorldVolumeEntry *pEntry = (WorldVolumeEntry*)p;
	if (pPartition)
	{
		aiCivWanderArea_RemoveVolume(pPartition, pEntry);
	}
}
// adds the given volume to every partition
void aiCivWanderArea_RemoveVolumeFromEachPartition(WorldVolumeEntry *pEntry)
{
	partition_ExecuteOnEachPartitionWithData(aiCivWanderArea_RemoveVolumeWrapper, pEntry);
}

// -------------------------------------------------------------------------------------------------------
void aiCivWanderArea_AddAllVolumesToPartition(AICivilianPartitionState *pPartition)
{
	FOR_EACH_IN_EARRAY(g_civSharedState.eaPedestrianWanderVolumes, WorldVolumeEntry, pVolume)
	{
		aiCivWanderArea_AddVolume(pPartition, pVolume);
	}
	FOR_EACH_END
}

// -------------------------------------------------------------------------------------------------------
void aiCivWanderArea_Shutdown(AICivilianPartitionState *pPartition)
{
	FOR_EACH_IN_EARRAY(pPartition->eaWanderAreas, AICivWanderArea, pWanderArea)
		// tell the peds that the area is being destroyed
		FOR_EACH_IN_EARRAY(pWanderArea->eaCivilians, AICivilianPedestrian, pPed)
			pPed->pWanderArea = NULL;
		FOR_EACH_END

	FOR_EACH_END

	eaDestroyEx(&pPartition->eaWanderAreas, aiCivWanderArea_Free);
}

// -------------------------------------------------------------------------------------------------------
static AICivWanderArea* aiCivWanderArea_FindWanderArea(AICivilianPartitionState *pPartition, const Vec3 vPos)
{
	const F32 fSearchRadius = 20.f;
	
	FOR_EACH_IN_EARRAY(pPartition->eaWanderAreas, AICivWanderArea, pWanderArea)
		if (pWanderArea->eaStartBeacons && 
			eaSize(&pWanderArea->eaCivilians) < pWanderArea->maxWanderers)
		{
			if (sphereSphereCollision(pWanderArea->vWorldMid, pWanderArea->fRadius, vPos, fSearchRadius))
			{
				return pWanderArea;
			}
		}
	FOR_EACH_END

	return NULL;
}

// -------------------------------------------------------------------------------------------------------
static Beacon* aiCivWanderArea_GetBestBeacon(AICivWanderArea *pWanderArea, const Vec3 vPos)
{
	F32 fClosestSQR = FLT_MAX;
	Beacon *pBestBeacon = NULL;

	FOR_EACH_IN_EARRAY(pWanderArea->eaStartBeacons, Beacon, pBeacon)
		F32 fDistSQR = distance3SquaredXZ(pBeacon->pos, vPos);
		if (fDistSQR < fClosestSQR)
		{
			fClosestSQR = fDistSQR;
			pBestBeacon = pBeacon;
		}
	FOR_EACH_END
	
	return pBestBeacon;
}

// -------------------------------------------------------------------------------------------------------
static int aiCivWanderArea_IsOtherCivUsingBeacon(const AICivWanderArea *pWanderArea,
												 const AICivilianPedestrian *pedIgnore,
												 const Beacon *pBeacon)
{
	FOR_EACH_IN_EARRAY(pWanderArea->eaCivilians, AICivilianPedestrian, pPed)
		if (pPed == pedIgnore)
			continue;
		if (pPed->pBeacon == pBeacon)
			return true;
	FOR_EACH_END

	return false;
}

// -------------------------------------------------------------------------------------------------------
static int aiCivWanderArea_IsPosInArea(const AICivWanderArea *pWanderArea, const Vec3 vPos)
{
	Vec3 vLocalPt, vTmp;

	// transform the point into the local box's space
	subVec3(vPos, pWanderArea->vMtxPos, vTmp);
	quatRotateVec3(pWanderArea->qInvRot, vTmp, vLocalPt);

	return pointBoxCollision(vLocalPt, pWanderArea->vLocalMin, pWanderArea->vLocalMax);
}

// -------------------------------------------------------------------------------------------------------
static void aiCivWanderArea_BeginUsing(AICivWanderArea* pWander, AICivilianPedestrian *ped)
{
	eaPush(&pWander->eaCivilians, ped);
}

// -------------------------------------------------------------------------------------------------------
static void aiCivWanderArea_StopUsing(AICivWanderArea* pWander, AICivilianPedestrian *ped)
{
	S32 idx = eaFindAndRemoveFast(&pWander->eaCivilians, ped);
	ped->pWanderArea = NULL;
}

