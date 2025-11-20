#include "aiCivilian.h"
#include "aiCivilianPrivate.h"
#include "aiCivilianTraffic.h"
#include "aiCivilianTrolley.h"
#include "aiCivilianCar.h"

#include "stdtypes.h"

#include "entCritter.h"
#include "Entity.h"
#include "gslInteractable.h"
#include "gslMapState.h"
#include "LineDist.h"
#include "mathutil.h"
#include "MemoryPool.h"
#include "interaction_common.h"
#include "rand.h"
#include "StashTable.h"
#include "StringCache.h"
#include "wlInteraction.h"
#include "wlSavedTaggedGroups.h"
#include "WorldLib.h"
#include "WorldGrid.h"

#include "AutoGen/wlSavedTaggedGroups_h_ast.h"

typedef struct AICivilian AICivilian;
typedef struct AICivilianTrafficQuery AICivilianTrafficQuery;

#define AICIV_TRAFFIC_PARANOID

#if defined(AICIV_TRAFFIC_PARANOID)
#define aicivtraffic_assert(exp)	(devassert(exp))
#else
#define aicivtraffic_assert(exp)	(void(0))
#endif

// ---------------------------------------------------------------

typedef enum EStopLightColor
{
	EStopLightColor_RED,
	EStopLightColor_YELLOW,
	EStopLightColor_GREEN
} EStopLightColor;


// ---------------------------------------------------------------
// Intersection / Traffic Manager
// ---------------------------------------------------------------
typedef struct AICivIntersectionManager
{
	AICivilianPathIntersection** 	eaStopLightList;
	S32								aitCurLight;
	AICivilianPathIntersection** 	eaStopSignList;
	S32								aitCurStopSign;

	U64								uLastUpdateTime;
} AICivIntersectionManager;


// ---------------------------------------------------------------
// AICivIntersectionManager functions
static AICivIntersectionManager* aiCivIntersectionManager_Create(S32 iPartitionIdx, AICivilianRuntimePathInfo *pathInfo);
static void aiCivIntersectionManager_Free(AICivIntersectionManager **isectManager);
static void aiCivilianIntersectionManagerUpdate(S32 iPartitionIdx, AICivIntersectionManager *isectManager);



// ---------------------------------------------------------------
typedef struct AICivilianTrafficQuery
{
	// bounding area
	Vec3		pos;			// the start position
	Vec3		dir;			// the direction
	Vec3		perp;
	F32			len;
	F32			half_width;		//

	EntityRef	requestee;		// the entity making the request, may be null

	AICivilianTrafficQuery	**eaSubQueries; // other queries that are needed to be satisfied

	const AICivilianPathLeg				**eaLegsList;
	const AICivilianPathIntersection	**eaIntersectionsList;

	S32			iPartitionIndex;
	U32			satisfied_query : 1;
} AICivilianTrafficQuery;

// ---------------------------------------------------------------
typedef struct AICivStopSignUser
{
	AICivilianCar*				user;
	Vec3						vStart;
	Vec3						vEnd;
	const AICivilianPathLeg*	srcLeg;
	const AICivilianPathLeg*	destLeg;
	U32							timer;
	AICivilianPathIntersection* acpi;
} AICivStopSignUser;

// ---------------------------------------------------------------
typedef struct AICivCrossTrafficManager
{
	S32							aitCurQuery;
	AICivilianTrafficQuery		**eaRequestQueue;	// queries yet to be initialized, once initialized, they go
	AICivilianTrafficQuery		**eaQueryList;		// to the eaQueryList

} AICivCrossTrafficManager;

// ---------------------------------------------------------------
// AICivCrossTrafficManager functions
static AICivCrossTrafficManager* aiCivCrossTrafficManagerCreate();
static void aiCivCrossTrafficManagerFree(AICivCrossTrafficManager **crosstraffic);
static void aiCivCrossTrafficManagerUpdate(AICivCrossTrafficManager *crosstraffic);



// ---------------------------------------------------------------
typedef struct AICivCarBlockManager
{
	AICivilianPathLeg			**eaActiveLegs;
	AICivilianPathIntersection	**eaActiveIntersections;

	// iterators
	S32							aitCurLeg;
	S32							aitCurIntersection;

} AICivCarBlockManager;


static AICivCarBlockManager* aiCivCarBlockManager_Create();
static void aiCivCarBlockManager_Free(AICivCarBlockManager **cbm);
static void aiCivCarBlockManager_Update(AICivCarBlockManager* cbm);



// ---------------------------------------------------------------
// Civilian Crosswalk users
// ---------------------------------------------------------------
typedef struct AICivCrosswalkManager
{
	AICivilianPathLeg		**eaActiveCrosswalks;
	S32						aitCurLeg;

} AICivCrosswalkManager;

typedef enum EUserCrossingState
{
	EUserCrossingState_FUTURE = 0,
	EUserCrossingState_WAITING,
	EUserCrossingState_CROSSING
} EUserCrossingState;

typedef struct AICivCrosswalkUser
{
	AICivilian				*pCiv;
	AICivilianPathLeg		*pCrosswalkLeg;
	S32						iPartitionIdx;

	U32		uReservedIndex : 4;
	U32		bReservedAtStart : 1;	// 
	U32		eCrossingState : 3;
		
} AICivCrosswalkUser;

static AICivCrosswalkManager* aiCivCrosswalkManager_Create();
static void aiCivCrosswalkManager_Free(AICivCrosswalkManager **cwm);
static void aiCivCrosswalkManager_Update(AICivCrosswalkManager* cwm);

static void aiCivCrosswalk_Open(AICivilianPathLeg *pLeg);
static void aiCivCrosswalk_Close(AICivilianPathLeg *pLeg);
static bool aiCivCrosswalk_IsActive(AICivilianPathLeg *pXwalk);
static bool aiCivCrosswalk_AreAnyCivsWaiting(AICivilianPathLeg *pXwalk);

static void aiCivTrolley_Update(AICivilianPartitionState *pPartition);

// ---------------------------------------------------------------

static const F32 s_fTrolleyCrossTime = 4.f;
static const F32 s_fSTOPLIGHT_GREEN_TIME = 10.0f;
static const F32 s_fSTOPLIGHT_YELLOW_TIME = 4.0f;

static const U32 s_iCrosswalkOpenFrequency = 7.0f;
static const U32 s_iCrosswalkOpenTime = 2.0f;

static U32 s_disableCrosswalks = 0;

#define INTERSECTION_MANAGER_UPDATE_TIME		3.0f

static int s_debugDrawCrossTrafficBlock = 0;

#define CROSS_TRAFFIC_MAX_UPDATES_PER_FRAME		2

#define STOPLIGHT_MAX_UPDATES_PER_FRAME			60
#define STOPSIGN_TIMEBEFORE_IGNORE_CAR			10

#define CARBLOCK_MAX_CHECKS_PER_FRAME			16

static S32 s_NUMBLOCKCHECK_PERFRAME = 250;

AUTO_CMD_INT(s_NUMBLOCKCHECK_PERFRAME, aiCivCarBlockChecksPerFrame);
AUTO_CMD_INT(s_debugDrawCrossTrafficBlock, aiCivDrawCrossTrafficBlock);
AUTO_CMD_INT(s_disableCrosswalks, aiCivDisableCrosswalks);


MP_DEFINE(AICivilianTrafficQuery);
MP_DEFINE(AICivStopSignUser);
MP_DEFINE(AICivCrosswalkUser);
MP_DEFINE(PathLegStopLight);

// ------------------------------------------------------------------------------------------------------------------
#define BLOCK_CODE_NONE		0
#define BLOCK_CODE_FIRST	1
#define BLOCK_CODE_SECOND	2

typedef struct AICivBlockInfo
{
	EBlockType	eBlockType;
	F32			fBlockDistSq;

} AICivBlockInfo;

// todo these should be gotten from a file, or based on actual collision distances
static const F32 s_fBlockWidth = 10.0f;
static const F32 s_fBlockCosTheta = 0.9848f;//cosf(RAD(10.0f));

static const F32 s_fBlockCrosswalkCautionDist = 45.0f;

static const F32 s_fBlockCriticalDistSQ[EAICivilianType_COUNT] = 
{
	0.f,
	SQR(15.f),
	SQR(40.f),
};

static const F32 s_fBlockCautionDistSQ[EAICivilianType_COUNT] = 
{
	0.f,
	SQR(40.f),
	SQR(80.f),
};


// ------------------------------------------------------------------------------------------------------------------
void aiCivTraffic_InitStatic()
{
	MP_CREATE(AICivilianTrafficQuery, 50);
	MP_CREATE(AICivStopSignUser, 50);
	MP_CREATE(AICivCrosswalkUser, 100);
	MP_CREATE(PathLegStopLight, 40);
	
}

PathLegStopLight* aiCivPathLegStopLight_Alloc()
{
	return MP_ALLOC(PathLegStopLight);
}

void aiCivPathLegStopLight_Free(PathLegStopLight* pli)
{
	REMOVE_HANDLE(pli->hStopLight);
	MP_FREE(PathLegStopLight, pli);
}

// PARTITION
// ------------------------------------------------------------------------------------------------------------------
void aiCivTraffic_Create(AICivilianPartitionState *pPartition)
{
	// no car legs, do not create the traffic
	if (eaSize(&pPartition->pathInfo.legs[1]) == 0)
	{
		return;
	}
		 
	pPartition->pCrossTrafficManager = aiCivCrossTrafficManagerCreate();
	devassert(pPartition->pCrossTrafficManager);
		
	pPartition->pIntersectionManager = aiCivIntersectionManager_Create(pPartition->iPartitionIndex, &pPartition->pathInfo);
	devassert(pPartition->pIntersectionManager);
		
	pPartition->pCarBlockManager = aiCivCarBlockManager_Create();
	devassert(pPartition->pCarBlockManager);
		
	pPartition->pCrosswalkManager = aiCivCrosswalkManager_Create();
	devassert(pPartition->pCrosswalkManager);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivTraffic_Destroy(AICivilianPartitionState *pPartition)
{
	aiCivCrosswalkManager_Free(&pPartition->pCrosswalkManager);
	aiCivCarBlockManager_Free(&pPartition->pCarBlockManager);
	aiCivIntersectionManager_Free(&pPartition->pIntersectionManager);
	aiCivCrossTrafficManagerFree(&pPartition->pCrossTrafficManager);	

}

// ------------------------------------------------------------------------------------------------------------------
void aiCivTraffic_OncePerFrame(AICivilianPartitionState *pPartition)
{
	if (! pPartition->pCrossTrafficManager)
	{	// if this is null, they all will be, so don't do the updates
		return;
	}

	PERFINFO_AUTO_START_FUNC();
	aiCivilianIntersectionManagerUpdate(pPartition->iPartitionIndex, pPartition->pIntersectionManager);
	aiCivCrossTrafficManagerUpdate(pPartition->pCrossTrafficManager);
	aiCivCarBlockManager_Update(pPartition->pCarBlockManager);
	aiCivCrosswalkManager_Update(pPartition->pCrosswalkManager);
	aiCivTrolley_Update(pPartition);
	PERFINFO_AUTO_STOP();
}



// ------------------------------------------------------------------------------------------------------------------

// Utility for throttling the updating by max update count
// TODO: create one that throttles by CPU time
S32 eaForEachPartial(S32 iPartitionIdx, EArrayHandle list, S32 aiCurIdx, S32 iMaxUpdates, fpUpdateFunc func)
{
	S32 i, endCount, iListSize;

	iListSize = eaSize(&list);
	if (iListSize == 0)
		return 0;

	if (aiCurIdx >= iListSize)
		aiCurIdx = 0;

	i = aiCurIdx;
	endCount = i + iMaxUpdates;
	if (endCount > iListSize)
		endCount = iListSize;

	while(i < endCount)
	{
		if (func(iPartitionIdx, list[i]))
		{
			eaRemoveFast(&list, i);
			iListSize--;
			endCount--;
			continue;
		}
		i++;
	}

	aiCurIdx = i;
	if (aiCurIdx >= iListSize)
	{
		aiCurIdx = 0;
	}

	return aiCurIdx;
}


// ------------------------------------------------------------------------------------------------------------------
// Stop Signs
// ------------------------------------------------------------------------------------------------------------------

void aiCivStopSignUserAdd(AICivilianPathIntersection *acpi, AICivilianCar *civ, const Vec3 vStart, const Vec3 vEnd,
								 const AICivilianPathLeg *destLeg, const AICivilianPathLeg *srcLeg)
{
	AICivStopSignUser *acssu;
	devassert( acpi->isectionType < EIntersectionType_marker_STOPLIGHT);
	devassert( civ->stopSign_user == NULL );

	acssu = MP_ALLOC(AICivStopSignUser);

	acssu->user = civ;
	acssu->acpi = acpi;

	copyVec3(vStart, acssu->vStart);
	copyVec3(vEnd, acssu->vEnd);
	acssu->destLeg = destLeg;
	acssu->srcLeg = srcLeg;
	acssu->timer = timeSecondsSince2000();


	civ->stopSign_user = acssu;
	eaPush(&acpi->eaStopSignUsers, acssu);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivStopSignUserFree(AICivStopSignUser **pacssu)
{
	if (*pacssu)
	{
		AICivStopSignUser *acssu = *pacssu;
		int idx;

		devassert(acssu->acpi && acssu->user);

		// remove it from the intersection list
		idx = eaFindAndRemove(&acssu->acpi->eaStopSignUsers, acssu);
		devassert(idx != -1);
		devassert(acssu->user->stopSign_user == acssu);

		acssu->user->stopSign_user = NULL;

		MP_FREE(AICivStopSignUser, acssu);
		*pacssu = NULL;
	}
}


// ------------------------------------------------------------------------------------------------------------------
bool aiCivStopSignIsWayClear(const AICivilianPathIntersection *acpi, const AICivilian *civ, const Vec3 vStart, const Vec3 vEnd,
									const AICivilianPathLeg *destLeg, const AICivilianPathLeg *srcLeg)
{
	S32 i;

	for( i = 0; i < eaSize(&acpi->eaStopSignUsers); i++)
	{
		Vec3 isect;
		AICivStopSignUser *acssu = acpi->eaStopSignUsers[i];
		if (acssu->destLeg == destLeg)
		{
			if (acssu->srcLeg == srcLeg)
			{	// ignore if we are starting from the same place
				continue;
			}

			return false;
		}

		// check if the line segments intersect
		if (lineSegLineSeg2dIntersection(acssu->vStart, acssu->vEnd, vStart, vEnd, isect))
		{
			return false;
		}
	}

	return true;
}


// ------------------------------------------------------------------------------------------------------------------
bool aiCivStopSignUserCheckACPI(const AICivStopSignUser *pacssu, const AICivilianPathIntersection *acpi)
{
	devassert(pacssu);
	return pacssu->acpi == acpi;
}


// ------------------------------------------------------------------------------------------------------------------
// Stop Light Manager
// ------------------------------------------------------------------------------------------------------------------
static void stopLight_SetOpenDirection(S32 iPartitionIdx, AICivilianPathIntersection *acpi, S32 idx);

static void aiCivIntersectionManager_Free(AICivIntersectionManager **pisectManager)
{
	if (*pisectManager)
	{
		AICivIntersectionManager *isectManager = *pisectManager;

		eaDestroy(&isectManager->eaStopLightList);
		isectManager->aitCurLight = 0;
		eaDestroy(&isectManager->eaStopSignList);
		isectManager->aitCurStopSign = 0;

		*pisectManager = NULL;
	}

	
}

// ------------------------------------------------------------------------------------------------------------------
static int cmpInteractionNodeDist(Vec3 vPos, const WorldInteractionNode **n1, const WorldInteractionNode **n2)
{
	F32 fdistN1Sq, fdistN2Sq;
	Vec3 vInteractPos;

	wlInteractionNodeGetWorldMid(*n1, vInteractPos);
	fdistN1Sq = distance3Squared(vInteractPos, vPos);
	
	wlInteractionNodeGetWorldMid(*n2, vInteractPos);
	fdistN2Sq = distance3Squared(vInteractPos, vPos);

	return (fdistN1Sq < fdistN2Sq) ? -1 : 1;
}

// ------------------------------------------------------------------------------------------------------------------
static bool interactionNode_IsStopLight(WorldInteractionNode *node, const char *pcTagType)
{
	WorldInteractionEntry *entry;
	WorldInteractionProperties *properties;

	// determine if this is a stopLight
	entry = wlInteractionNodeGetEntry(node);
	if (! entry) 
		return false;

	properties = entry->full_interaction_properties;
	if (! properties)
		return false;

	if (interaction_HasTag(properties, pcTagType))
		return true;
	
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static bool stopLight_AddStopLightNode(AICivilianPathIntersection *acpi, const Vec3 vACPIMid, F32 fACPIRadius, WorldInteractionNode *stopLightNode)
{
	Vec3 vNodePos, vLightDirection;
	S32 i;
	PathLegIntersect *bestPli = NULL;
	F32 fBestHeuristic;
	
	fBestHeuristic = -FLT_MAX;

	wlInteractionNodeGetWorldMid(stopLightNode, vNodePos);
	
	// get the facing direction of the light
	// the facing direction is assumed to be the negative X direction
	{
		Quat qRot;
		Mat3 mtxRot;

		wlInteractionNodeGetRot(stopLightNode, qRot);
		quatToMat(qRot, mtxRot);
		copyVec3(mtxRot[0], vLightDirection);
		negateVec3(vLightDirection, vLightDirection);
	}
	
	
	for (i = 0; i < eaSize(&acpi->legIntersects); i++)
	{
		PathLegIntersect *pli = acpi->legIntersects[i];
		const F32 *pvLegPos;
		F32 fFacingAngle;
		Vec3 vLegDirToACPI;

		copyVec3(pli->leg->dir, vLegDirToACPI);

		if (pli->leg->nextInt == acpi)
		{
			pvLegPos = pli->leg->end;
		}
		else 
		{
			devassert(pli->leg->prevInt == acpi);
			pvLegPos = pli->leg->start;
			negateVec3(vLegDirToACPI, vLegDirToACPI);
			
		}

		//
		fFacingAngle = getAngleBetweenNormalizedVec3(vLightDirection, vLegDirToACPI);
		fFacingAngle = PI - fFacingAngle;
		if (fFacingAngle < RAD(30.f))
		{
			PathLegStopLight *pPliStopLight = aiCivPathLegStopLight_Alloc();
			SET_HANDLE_FROM_REFERENT(INTERACTION_DICTIONARY, stopLightNode, pPliStopLight->hStopLight);
			eaPush(&pli->eaStopLights, pPliStopLight);
			return true;
		}

	}
	
	if (g_bAICivVerbose)
	{
		printf(	"\n\taiCivTraffic: Unable to find a matching leg for stoplight"
				"at (%.1f, %.1f, %.1f)\n", 
				vecParamsXYZ(vNodePos));
	}
	

	
	return false;
}

static PathLegIntersect* stopLight_GetCurrentOpenDirectionLeg(const AICivilianPathIntersection *acpi)
{
	if (acpi->iOpenDirectionIdx >= 0 && acpi->iOpenDirectionIdx < eaSize(&acpi->legIntersects) )
		return acpi->legIntersects[acpi->iOpenDirectionIdx];
	return NULL;
}

static void stopLight_OpenAllDirections(S32 iPartitionIdx, AICivilianPathIntersection *acpi);
// ------------------------------------------------------------------------------------------------------------------
static void stopLight_Init(S32 iPartitionIdx, AICivilianPathIntersection *acpi)
{
	
	Vec3 vMid;
	F32 fRadius;
	WorldInteractionNode **eaInteractionNodes = NULL;
	S32 i, numNodes;
	
	const char *pcNamedObject = allocAddString("NamedObject");
	const char *pcTypeTag = allocAddString("Traffic_Light");

	// init the first open direction, and create the array for the stop light nodes
	if (eaSize(&acpi->legIntersects) <= 2)
	{
		// if there are only two roads, then set both roads to green
		stopLight_OpenAllDirections(iPartitionIdx, acpi);
	}
	else
	{
		// set the first light green
		acpi->iOpenDirectionIdx = -1;
		stopLight_SetOpenDirection(iPartitionIdx, acpi, 0);
	}
	
	// try and locate the nearby streetlights and match the appropriate one to each pli
	interpVec3(0.5f, acpi->min, acpi->max, vMid);

	fRadius = distance3(acpi->min, acpi->max) * 0.5f;
	
	// get the interaction objects, sorted by distance to the intersection mid point
	{
		U32 classTypeMask;
		classTypeMask = wlInteractionClassNameToBitMask("NamedObject");
		wlInteractionQuerySphere(iPartitionIdx, classTypeMask, NULL, vMid, fRadius, false, false, true, &eaInteractionNodes);
		eaQSort_s(eaInteractionNodes, cmpInteractionNodeDist, vMid);
	}
	
	numNodes = eaSize(&eaInteractionNodes);
	for (i = 0; i < numNodes; i++)
	{	
		WorldInteractionNode *node = eaInteractionNodes[i];

		if (interactionNode_IsStopLight(node, pcTypeTag))
		{
			// determine which pli this is for
			stopLight_AddStopLightNode(acpi, vMid, fRadius, node);
				//numStopLightsNeeded --;
		}
	}
	
	eaDestroy(&eaInteractionNodes);
}

// ------------------------------------------------------------------------------------------------------------------
static AICivIntersectionManager* aiCivIntersectionManager_Create(S32 iPartitionIdx, AICivilianRuntimePathInfo *pathInfo)
{
	S32 i;
	AICivIntersectionManager *isectManager = calloc(1, sizeof(AICivIntersectionManager));
	devassert(isectManager);

	isectManager->uLastUpdateTime = 0;
	isectManager->aitCurLight = 0;
	isectManager->aitCurStopSign = 0;
	eaClear(&isectManager->eaStopLightList);
	eaClear(&isectManager->eaStopSignList);
		
	for (i = 0; i < eaSize(&pathInfo->intersects); i++)
	{
		AICivilianPathIntersection *acpi = pathInfo->intersects[i];
		
		if (acpi->legIntersects[0]->leg->type == EAICivilianLegType_CAR)
		{
			if (acpi->isectionType >= EIntersectionType_marker_STOPLIGHT)
			{
				eaPush(&isectManager->eaStopLightList, acpi);
				
				stopLight_Init(iPartitionIdx, acpi);
			}
			else if (acpi->isectionType == EIntersectionType_STOPSIGN)
			{
				eaPush(&isectManager->eaStopSignList, acpi);
			}
		}
	}

	return isectManager;
}

// ------------------------------------------------------------------------------------------------------------------
static AICivilianPathIntersection* aiCivIntersectionManager_FindClosestStopLight(AICivIntersectionManager *pIntersectionManager, const Vec3 vPos)
{
	F32 bestDist = FLT_MAX;
	AICivilianPathIntersection *pClosest = NULL;
	const F32 fMaxDistSQ = SQR(150.f);

	bestDist = fMaxDistSQ;

	FOR_EACH_IN_EARRAY(pIntersectionManager->eaStopLightList, AICivilianPathIntersection, acpi)
	{
		Vec3 vCenter;
		F32 dist;

		interpVec3(0.5f, acpi->min, acpi->max, vCenter);
		dist = distance3Squared(vCenter, vPos);

		if (dist < bestDist)
		{
			bestDist = dist;
			pClosest = acpi;
		}
	}
	FOR_EACH_END

	return pClosest;
}


// ------------------------------------------------------------------------------------------------------------------
// a testing command
AUTO_COMMAND;
void aiTraffic_ReacquireStopLights(Entity *e)
{
	if (e)
	{
		AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e));
		if (pPartition)
		{
			aiCivIntersectionManager_Free(&pPartition->pIntersectionManager);
			
			FOR_EACH_IN_EARRAY(pPartition->pathInfo.intersects, AICivilianPathIntersection, acpi)
				FOR_EACH_IN_EARRAY(acpi->legIntersects, PathLegIntersect, pli)
					eaClearEx(&pli->eaStopLights, aiCivPathLegStopLight_Free);
				FOR_EACH_END
			FOR_EACH_END

			// pIntersectionManager = aiCivIntersectionManager_Create();
		}
		
	}
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void stopLight_GetLegIntersectDirection(const AICivilianPathIntersection *acpi, const AICivilianPathLeg *leg, Vec2 v2TrafficDir)
{
	Vec2 v2IntersectionMid;
	const F32 *vLegPos;

	v2IntersectionMid[0] = (acpi->min[0] * 0.5f) + (acpi->max[0] * 0.5f);
	v2IntersectionMid[1] = (acpi->min[2] * 0.5f) + (acpi->max[2] * 0.5f);

	if (leg->nextInt == acpi)
	{
		vLegPos = leg->end;
	}
	else
	{
		vLegPos = leg->start;
	}

	v2TrafficDir[0] = vLegPos[0] - v2IntersectionMid[0];
	v2TrafficDir[1] = vLegPos[2] - v2IntersectionMid[1];
}

// ------------------------------------------------------------------------------------------------------------------

static void stopLight_SetRoadStateByIndex(S32 iPartitionIdx, AICivilianPathIntersection *acpi, S32 idx, EStopLightColor clr)
{
	if (idx >= 0 && idx < eaSize(&acpi->legIntersects) )
	{
		PathLegIntersect *pli = acpi->legIntersects[idx];

		FOR_EACH_IN_EARRAY(pli->eaStopLights, PathLegStopLight, pliStopLight)
		{
			WorldInteractionNode *pStopLight = GET_REF(pliStopLight->hStopLight);
			if (pStopLight)
			{	
				GameInteractable *pInteractable = interactable_GetByNode(pStopLight);
				interactable_SetVisibleChild(iPartitionIdx, pInteractable, clr, false);
			}
		}
		FOR_EACH_END
	}
}

/*
static void stopLight_SetDirectionGreen(PathLegIntersect *pli)
{
	FOR_EACH_IN_EARRAY(pli->eaStopLights, PathLegStopLight, pliStopLight)
	{
		WorldInteractionNode *pStopLight = GET_REF(pliStopLight->hStopLight);
		if (pStopLight)
		{	// make this light GREEN
			wlInteractionNodeSetVisibleChild(pStopLight, 2);
		}
	}
	FOR_EACH_END
}*/


static void stopLight_SetRoadState(S32 iPartitionIdx, PathLegIntersect *pli, EStopLightColor clr)
{
	FOR_EACH_IN_EARRAY(pli->eaStopLights, PathLegStopLight, pliStopLight)
	{
		WorldInteractionNode *pStopLight = GET_REF(pliStopLight->hStopLight);
		if (pStopLight)
		{	
			GameInteractable *pInteractable = interactable_GetByNode(pStopLight);
			interactable_SetVisibleChild(iPartitionIdx, pInteractable, clr, false);
		}
	}
	FOR_EACH_END
}


static void stopLight_SetAllDirectionsLightState(S32 iPartitionIdx, AICivilianPathIntersection *acpi, EStopLightColor clr)
{
	FOR_EACH_IN_EARRAY(acpi->legIntersects, PathLegIntersect, pli)
	{
		stopLight_SetRoadState(iPartitionIdx, pli, clr);
	}
	FOR_EACH_END
}

static void stopLight_OpenAllDirections(S32 iPartitionIdx, AICivilianPathIntersection *acpi)
{
	acpi->light_is_yellow = false;
	acpi->timeOfLastChange = ABS_TIME_PARTITION(iPartitionIdx);

	acpi->iOpenDirectionIdx = -1;

	FOR_EACH_IN_EARRAY(acpi->legIntersects, PathLegIntersect, pli)
	{
		stopLight_SetRoadState(iPartitionIdx, pli, EStopLightColor_GREEN);
		aiCivCrosswalk_Close(pli->pCrosswalkLeg);
	}
	FOR_EACH_END

}
F32 acgMath_GetAngleBetweenNormsAbs(const Vec3 v1, const Vec3 v2);

// ------------------------------------------------------------------------------------------------------------------
static void stopLight_SetOpenDirection(S32 iPartitionIdx, AICivilianPathIntersection *acpi, S32 idx)
{
	S32 iLastOpenIdx;
	// aicivtraffic_assert(idx < eaSize(&acpi->legIntersects));

	acpi->light_is_yellow = false;
	acpi->timeOfLastChange = ABS_TIME_PARTITION(iPartitionIdx);
	
	iLastOpenIdx = acpi->iOpenDirectionIdx;
	acpi->iOpenDirectionIdx = idx;

	{
		PathLegIntersect *pli;
				
		ANALYSIS_ASSUME(acpi->legIntersects);
		
		stopLight_SetRoadStateByIndex(iPartitionIdx, acpi, iLastOpenIdx, EStopLightColor_RED);
				
		pli = stopLight_GetCurrentOpenDirectionLeg (acpi); 
		if (pli)	
		{
			stopLight_SetRoadState(iPartitionIdx, pli, EStopLightColor_GREEN);

			// open any crosswalks 
			{
				S32 x = eaSize(&acpi->legIntersects) - 1;

				do {
					PathLegIntersect *otherPli = acpi->legIntersects[x];

					if (otherPli->pCrosswalkLeg)
					{
						F32 fAngle;
						if (pli == otherPli)
						{
							aiCivCrosswalk_Close(otherPli->pCrosswalkLeg);
							continue;
						}

						fAngle = acgMath_GetAngleBetweenNormsAbs(pli->leg->dir, otherPli->leg->dir);
						if (ABS(fAngle) > RAD(30.f))
						{
							// probably the opposite street, make sure the sidewalk is closed
							aiCivCrosswalk_Open(otherPli->pCrosswalkLeg);
						}
						else
						{
							// probably NOT the opposite street, open the crosswalk
							aiCivCrosswalk_Close(otherPli->pCrosswalkLeg);
						}
					}

				} while (--x >= 0);


			}
		}
		
	}
		
}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivStopLight_IsWayGreen(const AICivilianPathIntersection *acpi, const AICivilianPathLeg *leg)
{
	PathLegIntersect *pli;
	
	if (acpi->intersection_disabled)
		return false;
		
	pli = stopLight_GetCurrentOpenDirectionLeg(acpi);

	if (acpi->isectionType == EIntersectionType_2WAY_STOPLIGHT)
		return true;
	
	return (pli &&
			!acpi->light_is_yellow && !acpi->trolley_is_crossing && 
			(leg == pli->leg));
}

static void stopLight_CloseAllCrosswalks(AICivilianPathIntersection *acpi);
// ------------------------------------------------------------------------------------------------------------------
void aiCivStopLight_DisableIntersectionForXSecs(S32 iPartitionIdx, AICivilianPathIntersection *acpi, F32 fSeconds)
{
	acpi->intersection_disabled = true;
	
	stopLight_SetAllDirectionsLightState(iPartitionIdx, acpi, EStopLightColor_RED);
	stopLight_CloseAllCrosswalks(acpi);
	acpi->iOpenDirectionIdx = -1;

	// set the time when it will expire
	acpi->timeOfLastChange = ABS_TIME_PARTITION(iPartitionIdx) + SEC_TO_ABS_TIME(fSeconds);
}

// ------------------------------------------------------------------------------------------------------------------
static bool aiCivIntersection_DoesHaveTrolleyWaiting(AICivilianPathIntersection *acpi)
{
	return acpi->hFirstComeTrolley != 0;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivIntersection_TrolleyBeginCrossing(AICivilianPathIntersection *acpi)
{
	if (!acpi->trolley_is_crossing && acpi->hFirstComeTrolley)
	{
		Entity *pEnt = entFromEntityRefAnyPartition(acpi->hFirstComeTrolley);
		if (pEnt && pEnt->pCritter && pEnt->pCritter->civInfo)
		{
			int partitionIdx = entGetPartitionIdx(pEnt);
			pEnt->pCritter->civInfo->lastUpdateTime = ABS_TIME_PARTITION(partitionIdx) - SEC_TO_ABS_TIME(1.0f);
			
			acpi->trolley_is_crossing = true;
			acpi->timeOfLastChange = ABS_TIME_PARTITION(partitionIdx);
		}	
		else
		{
			acpi->hFirstComeTrolley = 0;
		}

	}
}
// ------------------------------------------------------------------------------------------------------------------
static bool aiCivIntersection_Update(S32 iPartitionIdx, AICivilianPathIntersection *acpi)
{
	if (acpi->intersection_disabled)
	{	
		if (acpi->timeOfLastChange < ABS_TIME_PARTITION(iPartitionIdx))
		{
			acpi->intersection_disabled = false;
			acpi->timeOfLastChange = ABS_TIME_PARTITION(iPartitionIdx) - SEC_TO_ABS_TIME(s_fSTOPLIGHT_YELLOW_TIME + 1.f);
			acpi->light_is_yellow = true;

			if (acpi->isectionType <= EIntersectionType_2WAY_STOPLIGHT)
			{
				stopLight_SetAllDirectionsLightState(iPartitionIdx, acpi, EStopLightColor_GREEN);
			}
		}

		return false;
	}

	if (acpi->trolley_is_crossing)
	{
		if (ABS_TIME_SINCE_PARTITION(iPartitionIdx, acpi->timeOfLastChange) > SEC_TO_ABS_TIME(s_fTrolleyCrossTime))
		{
			acpi->hFirstComeTrolley = 0;
			acpi->trolley_is_crossing = false;

			acpi->timeOfLastChange = ABS_TIME_PARTITION(iPartitionIdx) - SEC_TO_ABS_TIME(s_fSTOPLIGHT_YELLOW_TIME + 1.f);
			acpi->light_is_yellow = true;
			return true;
		}

		return false;
	}

	return true;

}


// ------------------------------------------------------------------------------------------------------------------
bool aiCivIntersection_CanTrolleyCross(AICivilianPathIntersection *acpi, AICivilianTrolley *pTrolley)
{
	if (acpi->intersection_disabled)
		return false;

	if (acpi->isectionType <= EIntersectionType_2WAY_STOPLIGHT)
		return true;

	// if (acpi->isectionType >= EIntersectionType_marker_STOPLIGHT)
	{
		if (!acpi->hFirstComeTrolley)
		{
			acpi->hFirstComeTrolley = pTrolley->civBase.myEntRef;
		}
		else if (acpi->hFirstComeTrolley == pTrolley->civBase.myEntRef)
		{
			return acpi->trolley_is_crossing;
		}
	}

	return false;

}


// ------------------------------------------------------------------------------------------------------------------
static void civUpdatePos(const AICivilian* civConst)
{
	AICivilian* civ = (AICivilian*)civConst;

	if(FALSE_THEN_SET(civ->v2PosIsCurrent))
	{
		Entity* e = entFromEntityRefAnyPartition(civ->myEntRef);
		
		if(e)
		{
			Vec3 vPos;
			
			entGetPos(e, vPos);
			civ->v2Pos[0] = vPos[0];
			civ->v2Pos[1] = vPos[2];
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void civUpdateDir(const AICivilian* civConst)
{
	AICivilian* civ = (AICivilian*)civConst;

	if (civ->type != EAICivilianType_TROLLEY)
	{
		if(FALSE_THEN_SET(civ->v2DirIsCurrent))
		{
			Entity* e = entFromEntityRefAnyPartition(civ->myEntRef);
			if(e)
			{
				Vec2 v2PitchYaw;
				Vec3 vFacingDir;

				entGetFacePY(e, v2PitchYaw);
				sincosf(v2PitchYaw[1], &vFacingDir[0], &vFacingDir[2]);
				vFacingDir[1] = 0.0f;

				civ->v2Dir[0] = vFacingDir[0];
				civ->v2Dir[1] = vFacingDir[2];
			}
		}
	}

	
}

// ------------------------------------------------------------------------------------------------------------------
static bool stopLight_AreCarsOncoming(const Vec2 v2TrafficDir, AICivilianPathLeg *leg)
{
#define LEN_TRAVERSE_THRESHOLD 	50.0f
	F32 fLen = 0.0f;

	do {
		StashTableIterator leg_iter;
		StashElement leg_elem;
		F32 fDot;

		stashGetIterator(leg->tracked_ents, &leg_iter);
		while(stashGetNextElement(&leg_iter, &leg_elem))
		{
			AICivilian *civ = stashElementGetPointer(leg_elem);
			civUpdateDir(civ);
			fDot = dotVec2(v2TrafficDir, civ->v2Dir);
			if (fDot < 0.0f)
				return true;
		}

		fLen += leg->len;
		if (fLen >= LEN_TRAVERSE_THRESHOLD)
			break;

		// get the next leg
		fDot = leg->dir[0] * v2TrafficDir[0] + leg->dir[2] * v2TrafficDir[1];
		if (fDot > 0.0f)
		{
			leg = leg->prev;
		}
		else
		{
			leg = leg->next;
		}


	} while(leg);


	return false;
}


// ------------------------------------------------------------------------------------------------------------------
// tests to see if there is a car waiting at the stop light line
static bool stopLight_IsCarWaiting(const Vec2 v2TrafficDir, const AICivilianPathIntersection *acpi, const AICivilianPathLeg *leg)
{
#define CAR_WAITING_DIST_THRESHOLDSQ	SQR(30.0f)

	F32 fLen = 0.0f;
	StashTableIterator leg_iter;
	StashElement leg_elem;
	F32 fDot;

	stashGetIterator(leg->tracked_ents, &leg_iter);
	while(stashGetNextElement(&leg_iter, &leg_elem))
	{
		AICivilian *civ = stashElementGetPointer(leg_elem);
		civUpdateDir(civ);
		fDot = dotVec2(v2TrafficDir, civ->v2Dir);
		if (fDot < 0.0f)
		{
			const F32 *pvLegPos;
			if (leg->nextInt == acpi)
			{
				pvLegPos = leg->end;
			}
			else
			{
				pvLegPos = leg->start;
			}

			if (distance3SquaredXZ(pvLegPos, civ->prevPos) < SQR(leg->width))
				return true; // cars are waiting
		}
	}

	
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
// given the pli index, see if the opposing leg crosswalks have people waiting
static S32 stopLight_ArePedsWaitingAtCrosswalks(const AICivilianPathIntersection *acpi, const PathLegIntersect *pli)
{
	S32 x = eaSize(&acpi->legIntersects) - 1;

	ANALYSIS_ASSUME(acpi->legIntersects);
	do {
		PathLegIntersect *otherPli = acpi->legIntersects[x];

		if (pli == otherPli) 
			continue;

		if (otherPli->pCrosswalkLeg)
		{
			F32 fAngle;
						
			fAngle = acgMath_GetAngleBetweenNormsAbs(pli->leg->dir, otherPli->leg->dir);
			if (ABS(fAngle) > RAD(30.f))
			{
				// 'probably' the opposite street
				if (aiCivCrosswalk_AreAnyCivsWaiting(otherPli->pCrosswalkLeg))
					return true;
			}
		}

	} while (--x >= 0);

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static S32 stopLight_GetNextOpenDirection(const AICivilianPathIntersection *acpi)
{
	S32 idx, nextIdx, i, numLegs;

	numLegs = eaSize(&acpi->legIntersects);
	
	// get the next index
	nextIdx = acpi->iOpenDirectionIdx;
	devassert(nextIdx != -1);
	nextIdx++;
	if (nextIdx >= numLegs)
		nextIdx = 0;

	ANALYSIS_ASSUME(acpi->legIntersects);
	i = 0;
	idx = nextIdx;
	do {
		const PathLegIntersect *pli = acpi->legIntersects[idx];
		Vec2 v2TrafficDir;

		stopLight_GetLegIntersectDirection(acpi, pli->leg, v2TrafficDir);

		if (stopLight_AreCarsOncoming(v2TrafficDir, pli->leg) || stopLight_ArePedsWaitingAtCrosswalks(acpi, pli))
		{
			return idx;
		}

		if (++idx >= numLegs)
			idx = 0;

	} while (++i < numLegs);

	return nextIdx;
}

// ------------------------------------------------------------------------------------------------------------------
static bool stopLight_CheckOtherLegsForWaitingCars(AICivilianPathIntersection *acpi)
{
	Vec2 v2TrafficDir;
	S32 numLegs = eaSize(&acpi->legIntersects);
	S32 i = 1;
	S32 idx = acpi->iOpenDirectionIdx + 1;

	ANALYSIS_ASSUME(acpi->legIntersects);
	
	do{
		PathLegIntersect *pli;
		if (idx >= numLegs)
			idx = 0;

		pli = acpi->legIntersects[idx];
		stopLight_GetLegIntersectDirection(acpi, pli->leg, v2TrafficDir);
		if (stopLight_IsCarWaiting(v2TrafficDir, acpi, pli->leg) )
		{
			return true;
		}

		idx++;
	} while(++i < numLegs);

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static bool stopLight_ShouldChangeLight(AICivilianPathIntersection *acpi)
{
	Vec2 v2TrafficDir;
	PathLegIntersect *openDirection;
	
	openDirection = stopLight_GetCurrentOpenDirectionLeg(acpi);
	if (!openDirection)
		return true;

	if (ABS_TIME_SINCE(acpi->timeOfLastChange) > SEC_TO_ABS_TIME(s_fSTOPLIGHT_GREEN_TIME))
	{
		/*
		if (!stopLight_CheckOtherLegsForWaitingCars(acpi))
		{
			acpi->timeOfLastChange = ABS_TIME_PARTITION(partitionIdx) - SEC_TO_ABS_TIME(s_fSTOPLIGHT_GREEN_TIME * 0.25f);
			return false;
		}
		*/

		return true;
	}


	stopLight_GetLegIntersectDirection(acpi, openDirection->leg, v2TrafficDir);

	if (! stopLight_AreCarsOncoming(v2TrafficDir, openDirection->leg))
	{	// no cars are coming, see if there is another direction that has cars waiting
		return aiCivIntersection_DoesHaveTrolleyWaiting(acpi) || 
				stopLight_CheckOtherLegsForWaitingCars(acpi);
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void stopLight_CloseAllCrosswalks(AICivilianPathIntersection *acpi)
{
	FOR_EACH_IN_EARRAY(acpi->legIntersects, PathLegIntersect, pli)
	{
		if (pli->pCrosswalkLeg)
		{
			aiCivCrosswalk_Close(pli->pCrosswalkLeg);
		}
	}
	FOR_EACH_END
}


// ------------------------------------------------------------------------------------------------------------------
static bool stopLight_Update(S32 iPartitionIdx, AICivilianPathIntersection *acpi)
{
	if (!aiCivIntersection_Update(iPartitionIdx, acpi))
		return false;
	if (acpi->isectionType == EIntersectionType_2WAY_STOPLIGHT)
		return false;

	if (acpi->light_is_yellow == true)
	{
		if (ABS_TIME_SINCE_PARTITION(iPartitionIdx, acpi->timeOfLastChange) > SEC_TO_ABS_TIME(s_fSTOPLIGHT_YELLOW_TIME))
		{
			// trying to make it so trolleys cannot hold up car traffic
			// it will take turns serving trolleys then car lanes
			if (aiCivIntersection_DoesHaveTrolleyWaiting(acpi))
			{
				aiCivIntersection_TrolleyBeginCrossing(acpi);
				acpi->light_is_yellow = false;
				stopLight_SetRoadStateByIndex(iPartitionIdx, acpi, acpi->iOpenDirectionIdx, EStopLightColor_RED);
			}
			else
			{
				S32 nextIdx = stopLight_GetNextOpenDirection(acpi);
				stopLight_SetOpenDirection(iPartitionIdx, acpi, nextIdx);
			}
		}
	}
	else
	{
		if (stopLight_ShouldChangeLight(acpi))
		{
			PathLegIntersect *pli = stopLight_GetCurrentOpenDirectionLeg(acpi);
			if (pli)
			{
				acpi->light_is_yellow = true;
				acpi->timeOfLastChange = ABS_TIME_PARTITION(iPartitionIdx);
				stopLight_SetRoadState(iPartitionIdx, pli, EStopLightColor_YELLOW);
				stopLight_CloseAllCrosswalks(acpi);
			}
			else
			{	// no light is on, next update change light
				acpi->light_is_yellow = true;
				acpi->timeOfLastChange = ABS_TIME_PARTITION(iPartitionIdx) - SEC_TO_ABS_TIME(s_fSTOPLIGHT_YELLOW_TIME + 1.f);
			}
			
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static bool aiCivilianUpdateStopSign(S32 partitionIdx, AICivilianPathIntersection *acpi)
{
	S32 i;

	U32 curTime = timeSecondsSince2000();
	for (i = eaSize(&acpi->eaStopSignUsers) - 1; i >= 0; i--)
	{
		AICivStopSignUser *user = acpi->eaStopSignUsers[i];
		if (curTime - user->timer > STOPSIGN_TIMEBEFORE_IGNORE_CAR)
		{
			aiCivStopSignUserFree(&user);
		}
	}

	return false;
}


static void aiCivilianIntersectionManagerUpdate(S32 iPartitionIdx, AICivIntersectionManager *isectManager)
{
	PERFINFO_AUTO_START_FUNC();

	if (ABS_TIME_SINCE_PARTITION(iPartitionIdx, isectManager->uLastUpdateTime) > SEC_TO_ABS_TIME(INTERSECTION_MANAGER_UPDATE_TIME))
	{
		isectManager->uLastUpdateTime = ABS_TIME_PARTITION(iPartitionIdx);

		isectManager->aitCurLight = eaForEachPartial(iPartitionIdx, isectManager->eaStopLightList, isectManager->aitCurLight, 
												STOPLIGHT_MAX_UPDATES_PER_FRAME, stopLight_Update);
		isectManager->aitCurStopSign = eaForEachPartial(iPartitionIdx, isectManager->eaStopSignList, isectManager->aitCurStopSign, 
												STOPLIGHT_MAX_UPDATES_PER_FRAME, aiCivilianUpdateStopSign);
	}

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
// cross traffic manager functions
// ------------------------------------------------------------------------------------------------------------------
static AICivCrossTrafficManager* aiCivCrossTrafficManagerCreate()
{
	return calloc(1, sizeof(AICivCrossTrafficManager));
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivCrossTrafficManagerFree(AICivCrossTrafficManager **crosstraffic)
{
	if (*crosstraffic)
	{	
		// all the AICivilianTrafficQuery are owned by the objects that request them
		eaDestroy(&(*crosstraffic)->eaRequestQueue);
		eaDestroy(&(*crosstraffic)->eaQueryList);

		free(*crosstraffic);
		*crosstraffic = NULL;
	}

}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivXTrafficQueryIsSatisfied(AICivilianTrafficQuery *query)
{
	devassert(query);
	return query->satisfied_query;
}

// ------------------------------------------------------------------------------------------------------------------
#define IN_LEG 0
#define OUT_END_OF_LEG 1
#define OUT_START_OF_LEG 2
#define OUT_WIDTH_OF_LEG 3
static S32 classifyPointRelativeLeg(const AICivilianPathLeg *leg, const Vec3 pt)
{
	Vec3 vLegToPt;
	F32  fDot;

	subVec3(pt, leg->start, vLegToPt);

	// dir
	fDot = dotVec3(vLegToPt, leg->dir);
	if (fDot < 0.0f)
		return OUT_END_OF_LEG;

	if (fDot > leg->len)
		return OUT_END_OF_LEG;

	fDot = dotVec3(vLegToPt, leg->perp);
	if(fabs(fDot) > (leg->width * 0.5f))
		return OUT_WIDTH_OF_LEG;

	return IN_LEG;
}

static void xtrafficCheckPointAddLegIfNeeded(AICivilianTrafficQuery *request, const AICivilianPathLeg *leg, const Vec3 pt)
{
	S32 relative;

	// I might also need to check mid intersections

	relative = classifyPointRelativeLeg(leg, pt);
	if (relative != IN_LEG)
	{
		// check what part of the leg to add
		switch(relative)
		{
			acase OUT_END_OF_LEG:
		if (leg->next)
		{
			eaPush(&request->eaLegsList, leg->next);
		}
		else if (leg->nextInt)
		{
			eaPush(&request->eaIntersectionsList, leg->nextInt);
		}

		xcase OUT_START_OF_LEG:
		if (leg->prev)
		{
			eaPush(&request->eaLegsList, leg->prev);
		}
		else if (leg->nextInt)
		{
			eaPush(&request->eaIntersectionsList, leg->prevInt);
		}
		xcase OUT_WIDTH_OF_LEG:
		break;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void xtrafficManagerProcessRequest(AICivilianTrafficQuery *request)
{
	Vec3 endPt;
	S32 i;
	// use the partition to get the legs/intersections we are intersecting

	scaleAddVec3(request->dir, request->len, request->pos, endPt);

	// init the perp vector of the request
	crossVec3Up(request->dir, request->perp);
	normalVec3(request->perp);


	// find all the legs that we should care about
	// this is really quick and dirty, and just is a means to get a good approximation of what
	// legs/intersections to include
	{
		const AICivilianPathLeg *leg = eaGet(&request->eaLegsList, 0);
		if (leg)
		{
			xtrafficCheckPointAddLegIfNeeded(request, leg, request->pos);
			xtrafficCheckPointAddLegIfNeeded(request, leg, endPt);
			//
		}
	}

	// debug draw the requests
	if (aiCivDebugRef && s_debugDrawCrossTrafficBlock)
	{
		Vec3 pos;
		Entity *debugger = NULL;

		debugger = entFromEntityRefAnyPartition(aiCivDebugRef);
		copyVec3(request->pos, pos);
		pos[1] += 1.0f;
		endPt[1] += 1.0f;
		wlAddClientLine(debugger, pos, endPt, 0xFFFF0000);

		scaleAddVec3(request->perp, request->half_width, request->pos, pos);
		scaleAddVec3(request->perp, -request->half_width, request->pos, endPt);
		pos[1] += 1.0f;
		endPt[1] += 1.0f;
		wlAddClientLine(debugger, pos, endPt, 0xFFFF0000);

		for (i = 0; i < eaSize(&request->eaLegsList); i++)
		{
			AICivilianPathLeg *leg = (AICivilianPathLeg*)request->eaLegsList[i];
			acgSendLeg(debugger, leg, 0xFFFF0000);
		}
	}

	// process any sub requests
	for (i = 0; i < eaSize(&request->eaSubQueries); i++)
	{
		xtrafficManagerProcessRequest(request->eaSubQueries[i]);
	}
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool xtrafficCheckBoundsForBlockers(const Vec3 pos, const AICivilianTrafficQuery *query)
{
	F32 fDot;
	Vec3 dirToBlockCheck;
	subVec3(pos, query->pos, dirToBlockCheck);

	fDot = dotVec3(dirToBlockCheck, query->dir);
	if (fDot >= 0.0f && fDot <= query->len)
	{
		fDot = dotVec3(dirToBlockCheck, query->perp);
		if (fabs(fDot) <= query->half_width)
		{
			// found a car in the bounding check, return
			return true;
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static bool xtrafficManagerProcessQuery(S32 iPartitionIdx, AICivilianTrafficQuery *query)
{
	S32 i;
	Vec3 pos;


	for (i = 0; i < eaSize(&query->eaLegsList); i++)
	{
		const AICivilianPathLeg *leg = query->eaLegsList[i];
		StashTableIterator leg_iter;
		StashElement leg_elem;

		stashGetIterator(leg->tracked_ents, &leg_iter);
		while(stashGetNextElement(&leg_iter, &leg_elem))
		{
			AICivilian *civ = stashElementGetPointer(leg_elem);
			if (civ->myEntRef != query->requestee)
			{
				Entity *ent;
				ent = entFromEntityRef(iPartitionIdx, civ->myEntRef);
				if (ent)
				{
					entGetPos(ent, pos);
					if( xtrafficCheckBoundsForBlockers(pos, query))
					{
						query->satisfied_query = false;
						return false;
					}
				}
			}
		}

	}

	for (i = 0; i < eaSize(&query->eaIntersectionsList); i++)
	{
		const AICivilianPathIntersection *acpi = query->eaIntersectionsList[i];
		StashTableIterator acpi_iter;
		StashElement acpi_elem;

		stashGetIterator(acpi->tracked_ents, &acpi_iter);
		while(stashGetNextElement(&acpi_iter, &acpi_elem))
		{
			AICivilian *civ = stashElementGetPointer(acpi_elem);
			if (civ->myEntRef != query->requestee)
			{
				Entity *ent;
				ent = entFromEntityRef(iPartitionIdx, civ->myEntRef);
				if (ent)
				{
					entGetPos(ent, pos);
					if( xtrafficCheckBoundsForBlockers(pos, query))
					{
						query->satisfied_query = false;
						return false;
					}
				}
			}
		}
	}

	if eaSize(&query->eaSubQueries)
	{
		// process any sub requests
		for (i = 0; i < eaSize(&query->eaSubQueries); i++)
		{
			AICivilianTrafficQuery *subQuery = query->eaSubQueries[i];
			xtrafficManagerProcessQuery(iPartitionIdx, subQuery);
			if(subQuery->satisfied_query == false)
			{
				query->satisfied_query = false;
				return false;
			}
		}

		query->satisfied_query = true;

	}


	query->satisfied_query = true;
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivCrossTrafficManagerUpdate(AICivCrossTrafficManager *crosstraffic)
{
	if (! crosstraffic)
	{
		return;
	}

	PERFINFO_AUTO_START_FUNC();

	if (eaSize(&crosstraffic->eaRequestQueue))
	{
		AICivilianTrafficQuery *request;
		// process one request per frame
		request = crosstraffic->eaRequestQueue[0];
		eaRemove(&crosstraffic->eaRequestQueue, 0);
		xtrafficManagerProcessRequest(request);

		eaPush(&crosstraffic->eaQueryList, request);
	}

	crosstraffic->aitCurQuery = eaForEachPartial(0, crosstraffic->eaQueryList, crosstraffic->aitCurQuery, 
											CROSS_TRAFFIC_MAX_UPDATES_PER_FRAME, xtrafficManagerProcessQuery);

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
AICivilianTrafficQuery* aiCivXTrafficCreateQuery(	S32 iPartitionIdx, AICivilian *civ, const Vec3 srcPs, const Vec3 dir,
													F32 len, F32 half_width, const AICivilianPathLeg *leg,
													AICivilianTrafficQuery *head_query)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	devassert(pPartition);
	if (pPartition->pCrossTrafficManager)
	{
		AICivilianTrafficQuery *request = MP_ALLOC(AICivilianTrafficQuery);

		request->iPartitionIndex = iPartitionIdx;

		request->eaIntersectionsList = NULL;
		request->eaLegsList = NULL;

		eaPush(&request->eaLegsList, leg);

		request->requestee = (civ != NULL) ? civ->myEntRef : 0;
		copyVec3(srcPs, request->pos);
		copyVec3(dir, request->dir);
		request->half_width = half_width;
		request->len = len;
		request->satisfied_query = false;
		request->eaSubQueries = NULL;

		if (head_query == NULL)
		{
			// if there is no head query, stick it on the main request queue
			eaPush(&pPartition->pCrossTrafficManager->eaRequestQueue, request);
		}
		else
		{
			// there is a head, put it on its list
			eaPush(&head_query->eaSubQueries, request);
		}

		return request;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivFreeTrafficQuery(AICivilianTrafficQuery *query)
{
	S32 i;

	for (i = 0; i < eaSize(&query->eaSubQueries); i++)
	{
		aiCivFreeTrafficQuery(query->eaSubQueries[i]);
	}

	eaDestroy(&query->eaSubQueries);
	eaDestroy(&query->eaIntersectionsList);
	eaDestroy(&query->eaLegsList);
	MP_FREE(AICivilianTrafficQuery, query);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCrossTrafficManagerReleaseQuery(AICivilianTrafficQuery **ppQuery)
{
	AICivilianPartitionState *pPartition;
	AICivCrossTrafficManager *pCrossTrafficManager;
	if (*ppQuery == NULL)
		return;

	pPartition = aiCivilian_GetPartitionState((*ppQuery)->iPartitionIndex);
	devassert(pPartition);
	pCrossTrafficManager = pPartition->pCrossTrafficManager;
	if (pCrossTrafficManager)
	{
		S32 idx = eaFindAndRemoveFast(&pCrossTrafficManager->eaQueryList, (*ppQuery));
		if (idx != -1)
		{
			if (idx < pCrossTrafficManager->aitCurQuery)
			{
				pCrossTrafficManager->aitCurQuery--;
			}
		}
		else
		{
			eaFindAndRemoveFast(&pCrossTrafficManager->eaRequestQueue, (*ppQuery));
		}
	}

	aiCivFreeTrafficQuery(*ppQuery);
	*ppQuery = NULL;
}


// ------------------------------------------------------------------------------------------------------------------
// AICivCarBlockManager
// ------------------------------------------------------------------------------------------------------------------

static AICivCarBlockManager* aiCivCarBlockManager_Create()
{
	AICivCarBlockManager *cbm = calloc(1, sizeof(AICivCarBlockManager));

	return cbm;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivCarBlockManager_Free(AICivCarBlockManager **pcbm)
{
	if (*pcbm)
	{
		AICivCarBlockManager *cbm = *pcbm;
		
		eaDestroy(&cbm->eaActiveLegs);
		eaDestroy(&cbm->eaActiveIntersections);

		free(cbm);
		
		*pcbm = NULL;
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCarBlockManager_ReportAddedLeg(AICivCarBlockManager *pCarBlockManager, AICivilianPathLeg *leg)
{
	if (pCarBlockManager)
	{
		if ( !leg->track_active && stashGetCount(leg->tracked_ents) >= 1)
		{
			if (!leg->track_active_added)
			{
				aicivtraffic_assert( eaFind(&pCarBlockManager->eaActiveLegs,leg) == -1 );
				leg->track_active_added = true;
				eaPush(&pCarBlockManager->eaActiveLegs, leg);
			}
			
			leg->track_active = true;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCarBlockManager_ReportRemovedLeg(AICivilianPathLeg *leg)
{
	// if we're at 1, flag the removal of the leg
	if (leg->track_active && stashGetCount(leg->tracked_ents) == 0)
	{
		leg->track_active = false;
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCarBlockManager_ReportAddedIntersection(AICivCarBlockManager *pCarBlockManager, AICivilianPathIntersection *acpi)
{
	if (pCarBlockManager)
	{
		// only add a new leg if we're at or above 2 
		if ( !acpi->track_active && stashGetCount(acpi->tracked_ents) >= 2)
		{
			if (!acpi->track_active_added)
			{
				aicivtraffic_assert( eaFind(&pCarBlockManager->eaActiveIntersections,acpi) == -1 );
				acpi->track_active_added = true;
				eaPush(&pCarBlockManager->eaActiveIntersections, acpi);
			}
			
			acpi->track_active = true;
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCarBlockManager_ReportRemovedIntersection(AICivilianPathIntersection *acpi)
{
	// if we're at 1 or less, flag the removal of the intersection
	if (acpi->track_active && stashGetCount(acpi->tracked_ents) <= 1)
	{
		acpi->track_active = false;
	}

}





// ------------------------------------------------------------------------------------------------------------------
__forceinline static F32 blockDistSqToBlockType(F32 fBlockDistSq, EAICivilianType type)
{

	if (fBlockDistSq <= s_fBlockCriticalDistSQ[type])
	{
		return EBlockType_CRITICAL;
	}
	
	if (fBlockDistSq <= s_fBlockCautionDistSQ[type])
	{
		return EBlockType_CAUTION;
	}

	return EBlockType_NONE;
}

#define OPPOSING_DIRECTION_COSTHETA		(0.35f)  //~70 degrees
#define BLOCKING_CONE_COSTHETA			(0.866f) //~45 deg // (0.866f)  //~30 degrees

// ------------------------------------------------------------------------------------------------------------------
static F32 checkBlockDist(const AICivilian *civ, const AICivilian *other_civ)
{
	Vec2 vToCar;
	F32 fDistSq, fDot;

	//if (civ->lane != other_civ->lane)
	//	return FLT_MAX;

	// if they are in opposing directions
	civUpdateDir(civ);
	civUpdateDir(other_civ);
	
	if (dotVec2(civ->v2Dir, other_civ->v2Dir) <= OPPOSING_DIRECTION_COSTHETA)
		return FLT_MAX;
		
	civUpdatePos(other_civ);
	civUpdatePos(civ);

	// dir from civ to other_civ
	subVec2(other_civ->v2Pos, civ->v2Pos, vToCar);

	fDistSq = lengthVec2Squared(vToCar);
	if (fDistSq > s_fBlockCautionDistSQ[other_civ->type])
		return FLT_MAX; // too far from each other

	fDot = dotVec2(vToCar, civ->v2Dir);
	if (fDot <= 0) // behind 
		return FLT_MAX;

	fDot = fDot / sqrtf(fDistSq);
	

	return (fDot >= BLOCKING_CONE_COSTHETA) ? fDistSq : FLT_MAX;
}


// ------------------------------------------------------------------------------------------------------------------
#define dotPerpVec2(vp1,v2)	(-(vp1)[1]*(v2)[0] + (vp1)[0]*(v2)[1])
#define dotNegVec2(vn1, v2) (-(vn1)[0]*(v2)[0] + -(vn1)[1]*(v2)[1])
// ------------------------------------------------------------------------------------------------------------------
static S32 checkForBlocking(const AICivilian *pCiv, const AICivilian *pCivOther, F32 *pfBlockDistSq)
{
	Vec2 vToCar;
	F32 fDistSq, fDot;

	// cars are facing in opposite directions
	civUpdateDir(pCiv);
	civUpdateDir(pCivOther);

	if (dotVec2(pCiv->v2Dir, pCivOther->v2Dir) <= OPPOSING_DIRECTION_COSTHETA)
		return BLOCK_CODE_NONE;

	civUpdatePos(pCivOther);
	civUpdatePos(pCiv);

	// dir from first to second car
	subVec2(pCivOther->v2Pos, pCiv->v2Pos, vToCar);

	fDistSq = lengthVec2Squared(vToCar);
	if (fDistSq > s_fBlockCautionDistSQ[pCivOther->type])
		return BLOCK_CODE_NONE; // too far from each other

	*pfBlockDistSq = fDistSq;
	fDistSq = sqrtf(fDistSq);

	// find out which car is blocking which
	fDot = dotVec2(vToCar, pCiv->v2Dir);
	//return (fDot > 0) ? BLOCK_CODE_FIRST : BLOCK_CODE_SECOND;

	if (fDot > 0.0f)
	{
		// check to see if the car is within the cone of blocking
		fDot = fDot / fDistSq;

		return (fDot >= BLOCKING_CONE_COSTHETA) ? BLOCK_CODE_FIRST : BLOCK_CODE_NONE;
	}
	else if (fDot < 0.0f)
	{
		// negate the dot because the vToCar is in the opposite direction
		fDot = -dotVec2(vToCar, pCivOther->v2Dir); 
		fDot = fDot / fDistSq;

		return (fDot >= BLOCKING_CONE_COSTHETA) ? BLOCK_CODE_SECOND : BLOCK_CODE_NONE;
	}

	return BLOCK_CODE_NONE;
}

// ------------------------------------------------------------------------------------------------------------------
static bool checkCrosswalkBlockingCar(const AICivilianPathLeg *pXWalk, const AICivilian *pCiv, F32 *pfBlockDistSq);

// ------------------------------------------------------------------------------------------------------------------
void aiCivBlock_Initialize(AICivilianBlockInfo *pBlock)
{
	pBlock->fBlockDistSq = FLT_MAX;
}

// ------------------------------------------------------------------------------------------------------------------
EBlockType aiCivBlock_UpdateBlocking(AICivilian *pBlockee, AICivilianBlockInfo *pBlockInfo)
{
	if (pBlockInfo->entRefBlocker)
	{
		Entity *otherCiv = entFromEntityRefAnyPartition(pBlockInfo->entRefBlocker);
		if (otherCiv && otherCiv->pCritter && otherCiv->pCritter->civInfo)
		{
			F32 fDistSq = checkBlockDist(pBlockee, otherCiv->pCritter->civInfo);

			if (fDistSq > 0.005f && fDistSq <= s_fBlockCautionDistSQ[pBlockee->type])
			{
				pBlockInfo->fBlockDistSq = fDistSq;
				return EBlockType_CAUTION;
			}
		}
	}
	else if (pBlockInfo->pBlockingCrosswalk)
	{
		F32 fDistSq;
		if (aiCivCrosswalk_IsActive(pBlockInfo->pBlockingCrosswalk) && 
			checkCrosswalkBlockingCar(pBlockInfo->pBlockingCrosswalk, pBlockee, &fDistSq) )
		// if (fDistSq > 0.005f && fDistSq <= s_fBlockCrosswalkCautionDist)
		{
			// civ->fBlockDistSq = fDistSq;
			return EBlockType_CAUTION;
		}
	}

	pBlockInfo->pBlockingCrosswalk = NULL;
	pBlockInfo->entRefBlocker = 0;
	pBlockInfo->fBlockDistSq = FLT_MAX;
	pBlockInfo->eBlockType = EBlockType_NONE;
	return EBlockType_NONE;
}



// ------------------------------------------------------------------------------------------------------------------
static S32 checkCarsForBlocking(const AICivilianCar *pCiv, const AICivilianCar *pCivOther, F32 *pfBlockDistSq)
{
	if (pCiv->lane != 0 && pCivOther->lane != 0 && pCiv->lane != pCivOther->lane)
		return BLOCK_CODE_NONE;

	return checkForBlocking(&pCiv->civBase, &pCivOther->civBase, pfBlockDistSq);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivBlock_NewBlocker(AICivilian *pBlockee, AICivilianBlockInfo *pBlockeeInfo, const AICivilian *blocker, F32 fBlockDistSq)
{
	if (pBlockeeInfo->fBlockDistSq > fBlockDistSq)
	{
		EBlockType eOldBlockType = pBlockeeInfo->eBlockType;
		pBlockeeInfo->entRefBlocker = blocker->myEntRef;
		pBlockeeInfo->pBlockingCrosswalk = NULL;

		pBlockeeInfo->fBlockDistSq = fBlockDistSq;
		
		pBlockeeInfo->eBlockType = blockDistSqToBlockType(fBlockDistSq, pBlockee->type);
		if (pBlockeeInfo->eBlockType > eOldBlockType && pBlockeeInfo->eBlockType >= EBlockType_CLOSE)
		{
			// force an update if we have a newer block type
			pBlockee->lastUpdateTime = ABS_TIME_PARTITION(pBlockee->iPartitionIdx) - SEC_TO_ABS_TIME(1.0f);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivBlock_NewCrosswalkBlock(AICivilian *pBlockee, AICivilianBlockInfo *pBlockeeInfo, AICivilianPathLeg *pXWalk, F32 fBlockDistSq)
{
	if (pBlockeeInfo->fBlockDistSq > fBlockDistSq)
	{
		EBlockType eOldBlockType = pBlockeeInfo->eBlockType;

		pBlockeeInfo->pBlockingCrosswalk = pXWalk;
		pBlockeeInfo->entRefBlocker = 0;
		pBlockeeInfo->fBlockDistSq = fBlockDistSq;

		pBlockeeInfo->eBlockType = blockDistSqToBlockType(fBlockDistSq, pBlockee->type);
		if (pBlockeeInfo->eBlockType > eOldBlockType && pBlockeeInfo->eBlockType >= EBlockType_CLOSE)
		{
			// force an update if we have a newer block type
			pBlockee->lastUpdateTime = ABS_TIME_PARTITION(pBlockee->iPartitionIdx) - SEC_TO_ABS_TIME(1.0f);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
#define subVec3Vec2(v31,v22,r)	((r)[0] = (v31)[0]-(v22)[0], (r)[1] = (v31)[2]-(v22)[1])
#define subVec2Vec3(v21,v32,r)	((r)[0] = (v21)[0]-(v32)[0], (r)[1] = (v21)[1]-(v32)[2])
#define dotVec3Vec2(v31,v22)	((v31)[0]*(v22)[0] + (v31)[2]*(v22)[1])

// ------------------------------------------------------------------------------------------------------------------
__forceinline static S32 aiCivCrosswalk_GetLane(F32 fDistFromXWalkStart, const AICivilianPathLeg *pXWalkLeg)
{
	AICivilianPathLeg *pRoad = pXWalkLeg->pCrosswalkNearestRoad;

	fDistFromXWalkStart -= pXWalkLeg->crosswalkRoadStartDist;

	if (pRoad->median_width > 0.f)
	{
		F32 fDirWidth = pRoad->max_lanes * pRoad->lane_width;
		if (fDistFromXWalkStart > fDirWidth + pRoad->median_width)
		{
			fDistFromXWalkStart -= pRoad->median_width;
		}
		else if (fDistFromXWalkStart > fDirWidth)
		{	// within the median
			return -1;
		}
	}

	return (S32)floorf(fDistFromXWalkStart / pXWalkLeg->lane_width);
}

static bool checkCrosswalkBlockingCar(const AICivilianPathLeg *pXWalk, const AICivilian *pCiv, F32 *pfBlockDistSq)
{
	// check if the leg is in front of the car
	Vec2 v2LegToCar;
	F32 fDist, fDistAlongLeg;
	U32 iLaneBlock;

	civUpdatePos(pCiv);

	// find the nearest point on the crosswalk relative to the car's position
	// we can more accurately determine if we are in front and facing the crosswalk
	{
		Vec3 vCivPos;
		Vec3 vCol;
		vCivPos[0] = pCiv->v2Pos[0];
		vCivPos[1] = pXWalk->start[1];
		vCivPos[2] = pCiv->v2Pos[1];

		PointLineDistSquared(vCivPos, pXWalk->start, pXWalk->dir, pXWalk->len, vCol);

		subVec2Vec3(pCiv->v2Pos, vCol, v2LegToCar);
	}
	
	
	civUpdateDir(pCiv);

	fDist = dotVec2(pCiv->v2Dir, v2LegToCar);
	if (fDist >= 0.0f)
		return false;  // the crosswalk is behind

	// check the angle between the car facing and the direction to the crosswalk 
	{
		F32 fLegToCarLen = lengthVec2(v2LegToCar);
		if (fLegToCarLen == 0.f)
			return false;
		
		fDist = fDist / fLegToCarLen;
		fDist = ABS(fDist);
		if (fDist < 0.25f) //  cos 75
			return false; // angle difference is greather than 45 degrees
	}

	fDist = dotVec3Vec2(pXWalk->perp, v2LegToCar);
	fDist = ABS(fDist);
	if (fDist > s_fBlockCrosswalkCautionDist)
		return false; // the crosswalk is too far
	//if (fDist < pXWalk->width)
	//	return false; // already in the crosswalk, just hope we keep going

	subVec2Vec3(pCiv->v2Pos, pXWalk->start, v2LegToCar);

	// check what lane the car needs and determine if that lane is blocked
	fDistAlongLeg = dotVec3Vec2(pXWalk->dir, v2LegToCar);
	if (fDistAlongLeg <= 0.0f || fDistAlongLeg >= pXWalk->len)
		return false;

	iLaneBlock = aiCivCrosswalk_GetLane(fDistAlongLeg, pXWalk);
	//(S32)floorf(fDistAlongLeg / pXWalk->lane_width);
	if ((BIT(iLaneBlock) & pXWalk->uLanesBlockedMask) == 0)
		return false;
	
	*pfBlockDistSq = SQR(fDist);
	return true;

	
}

// ------------------------------------------------------------------------------------------------------------------
static bool checkBlockersOnLeg(S32 iPartitionIdx, AICivilianPathLeg *leg)
{
	StashTableIterator	itOuter;
	StashElement		elem;
	S32 numCrosswalks;
		
	if (leg->track_active == false)
	{
		leg->track_active_added = false;
		return true; // remove this leg from the active list
	}
	
	stashGetIterator(leg->tracked_ents, &itOuter);

	while(stashGetNextElement(&itOuter, &elem))
	{
		StashTableIterator	itInner;
		AICivilianCar *civOuter = stashElementGetPointer(elem);
		
		// check each other car vs the outer
		itInner = itOuter;
		while(stashGetNextElement(&itInner, &elem))
		{
			AICivilianCar *civInner = stashElementGetPointer(elem);
			F32 fBlockDistSq;
			S32 iBlockCode = checkCarsForBlocking(civInner, civOuter, &fBlockDistSq);

			if (iBlockCode != BLOCK_CODE_NONE)
			{
				if (iBlockCode == BLOCK_CODE_FIRST)
				{
					aiCivBlock_NewBlocker(&civInner->civBase, &civInner->blockInfo, &civOuter->civBase, fBlockDistSq);
				}
				else
				{
					aiCivBlock_NewBlocker(&civOuter->civBase, &civOuter->blockInfo, &civInner->civBase, fBlockDistSq);
				}
			}
		}
	}


	numCrosswalks = eaSize(&leg->eaCrosswalkLegs);
	// check the crosswalks vs this car
	if (numCrosswalks)
	{
		S32 i = numCrosswalks - 1;
		
		// check if any of the crosswalks have any users, if not
		// exit from the function
		{	
			bool bHasActiveCrosswalk = false;

			do {
				AICivilianPathLeg *xwalk = leg->eaCrosswalkLegs[i];
				if (aiCivCrosswalk_IsActive(xwalk))
				{	
					bHasActiveCrosswalk = true;
					break;
				}
			} while(--i >= 0);

			if (! bHasActiveCrosswalk)
			{
				return false;
			}
		}
		

		stashGetIterator(leg->tracked_ents, &itOuter);
		while(stashGetNextElement(&itOuter, &elem))
		{
			AICivilianCar *civ = stashElementGetPointer(elem);
			
			i = numCrosswalks - 1;

			do {
				AICivilianPathLeg *pXWalk = leg->eaCrosswalkLegs[i];
				if (aiCivCrosswalk_IsActive(pXWalk))
				{	
					F32 fBlockDistSq;
					// xwalk is active, check if the xwalk is blocking the car
					if ( checkCrosswalkBlockingCar(pXWalk, &civ->civBase, &fBlockDistSq) )
					{
						aiCivBlock_NewCrosswalkBlock(&civ->civBase, &civ->blockInfo, pXWalk, fBlockDistSq);
					}
				}
			} while(--i >= 0);
			
		}

	}
	
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static bool checkBlocksOnIntersection(S32 iPartitionIdx, AICivilianPathIntersection *acpi)
{
	StashTableIterator	itOuter;
	StashElement		elem;


	if (acpi->track_active == false)
	{
		acpi->track_active_added = false;

		return true; // remove this intersection from the active list
	}

	stashGetIterator(acpi->tracked_ents, &itOuter);

	while(stashGetNextElement(&itOuter, &elem))
	{
		StashTableIterator	itInner;
		AICivilianCar *civOuter = stashElementGetPointer(elem);

		itInner = itOuter;
		while(stashGetNextElement(&itInner, &elem))
		{
			AICivilianCar *civInner = stashElementGetPointer(elem);
			F32 fBlockDistSq;
			S32 iBlockCode = checkCarsForBlocking(civInner, civOuter, &fBlockDistSq);

			if (iBlockCode != BLOCK_CODE_NONE)
			{
				if (iBlockCode == BLOCK_CODE_FIRST)
				{
					aiCivBlock_NewBlocker(&civInner->civBase, &civInner->blockInfo, &civOuter->civBase, fBlockDistSq);
				}
				else
				{
					aiCivBlock_NewBlocker(&civOuter->civBase, &civOuter->blockInfo, &civInner->civBase, fBlockDistSq);
				}
			}

		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivCarBlockManager_Update(AICivCarBlockManager* cbm)
{
	PERFINFO_AUTO_START_FUNC();

	if (cbm)
	{
		S32 numToProcess;
		
		PERFINFO_AUTO_START("CheckBlockersOnLeg", 1);
		numToProcess = s_NUMBLOCKCHECK_PERFRAME;
		cbm->aitCurLeg = eaForEachPartial(0, cbm->eaActiveLegs, cbm->aitCurLeg, numToProcess, checkBlockersOnLeg);

		PERFINFO_AUTO_STOP_START("checkBlocksOnIntersection", 1);
		numToProcess = s_NUMBLOCKCHECK_PERFRAME;
		cbm->aitCurIntersection = eaForEachPartial(0, cbm->eaActiveIntersections, cbm->aitCurIntersection, numToProcess, checkBlocksOnIntersection);
		PERFINFO_AUTO_STOP();
	}

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
AUTO_COMMAND;
void aiTraffic_ShowStreetRelations()
{
	Entity *debugger;
	debugger = entFromEntityRefAnyPartition(aiCivDebugRef);
	if (debugger)
	{
		S32 i;
		AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(debugger));
		devassert(pPartition);

		for (i = 0; i < eaSize(&pPartition->pathInfo.intersects); i++)
		{
			const AICivilianPathIntersection *acpi = pPartition->pathInfo.intersects[i];

			if (acpi->isectionType >= EIntersectionType_marker_STOPLIGHT)
			{
				S32 x;
				for( x = 0; x < eaSize(&acpi->legIntersects); x++)
				{
					const PathLegIntersect *pli = acpi->legIntersects[x];

					FOR_EACH_IN_EARRAY(pli->eaStopLights, PathLegStopLight, pliStopLight)
					{
						const WorldInteractionNode *stopLight = GET_REF(pliStopLight->hStopLight);
						if (stopLight)
						{
							Vec3 vStart, vEnd;

							if (pli->leg->nextInt == acpi) 
								copyVec3(pli->leg->end, vStart);
							else
								copyVec3(pli->leg->start, vStart);

							vStart[1] += 4.0f;
							wlInteractionNodeGetWorldMid(stopLight, vEnd);
							vEnd[1] += 4.0f;

							wlAddClientLine(debugger, vStart, vEnd, 0xFF00FF00 );
						}
					}
					FOR_EACH_END
					
					
				}
				

			}
		}
	}

	
}

// ------------------------------------------------------------------------------------------------------------------
// AICivCrosswalkManager
// ------------------------------------------------------------------------------------------------------------------
static bool aiCivCrosswalk_Update(S32 iPartitionIdx, AICivilianPathLeg *leg);

static AICivCrosswalkManager* aiCivCrosswalkManager_Create()
{
	return calloc(1, sizeof(AICivCrosswalkManager));
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivCrosswalkManager_Free(AICivCrosswalkManager **pcwm)
{
	if(*pcwm)
	{
		AICivCrosswalkManager *cwm = *pcwm;

		eaDestroy(&cwm->eaActiveCrosswalks);

		free(cwm);
		*pcwm = NULL;
	}
	
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivCrosswalkManager_Update(AICivCrosswalkManager* cwm)
{
	PERFINFO_AUTO_START_FUNC();

	if (cwm)
	{
		#define NUM_CROSSWALKS_TO_PROCESS	3
		cwm->aitCurLeg = eaForEachPartial(0, cwm->eaActiveCrosswalks, cwm->aitCurLeg, NUM_CROSSWALKS_TO_PROCESS, aiCivCrosswalk_Update);
	}

	PERFINFO_AUTO_STOP();
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivCrosswalk_Close(AICivilianPathLeg *pLeg)
{
	if (pLeg)
		pLeg->bIsCrosswalkOpen = false;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiCivCrosswalk_Open(AICivilianPathLeg *pLeg)
{
	if (pLeg)
	{
		pLeg->iCrosswalkTimer = timeSecondsSince2000();
		pLeg->bIsCrosswalkOpen = true;
	}
	
}



// ------------------------------------------------------------------------------------------------------------------
static bool aiCivCrosswalk_Update(S32 iPartitionIdx, AICivilianPathLeg *pLeg)
{
	U32 numUsers;

	if (pLeg->bIsCrosswalkOpen)
	{
		U32 iTimeSince = timeSecondsSince2000() - pLeg->iCrosswalkTimer;
		if (iTimeSince > s_iCrosswalkOpenTime)
		{
			pLeg->iCrosswalkTimer = timeSecondsSince2000();
			pLeg->bIsCrosswalkOpen = false;
		}
	}
	else
	{
		if (pLeg->pXTrafficQuery && aiCivXTrafficQueryIsSatisfied(pLeg->pXTrafficQuery))
		{
			pLeg->iCrosswalkTimer = timeSecondsSince2000();
			pLeg->bIsCrosswalkOpen = true;
			
			aiCivCrossTrafficManagerReleaseQuery(&pLeg->pXTrafficQuery);
		}

	}

	pLeg->uLanesBlockedMask = 0;

	// if there are crosswalk users, determine which lanes they are blocking.
	numUsers = eaSize(&pLeg->eaCrosswalkUsers);
	if (numUsers && pLeg->pCrosswalkNearestRoad)
	{
		#define LANE_BLOCK_THRESHOLD	2.5f

		F32 fDistAlongLeg;
		S32 iLaneBlock;//, iLaneBlockMin, iLaneBlockMax;
		Vec2 v2StartToCivPos;
		S32 i = numUsers - 1;
		F32 invLaneWidth = 1.0f / pLeg->lane_width;
		U32 uAllLanesBlockedMask = BIT_RANGE(0, pLeg->max_lanes);
		F32 roadWidth = pLeg->pCrosswalkNearestRoad->width;
		F32 halfMedian = pLeg->pCrosswalkNearestRoad->median_width * .5f;
		F32 directionWidth = pLeg->pCrosswalkNearestRoad->max_lanes * pLeg->lane_width;

		do {
			AICivCrosswalkUser *pUser = pLeg->eaCrosswalkUsers[i];			

			if (pUser->eCrossingState == EUserCrossingState_CROSSING)
			{
				AICivilian *pCiv = pUser->pCiv;
				
				civUpdatePos(pCiv);
				
				subVec2Vec3(pCiv->v2Pos, pLeg->start, v2StartToCivPos);
				fDistAlongLeg = dotVec3Vec2(pLeg->dir, v2StartToCivPos) - pLeg->crosswalkRoadStartDist;
					
				if (fDistAlongLeg < LANE_BLOCK_THRESHOLD || fDistAlongLeg > (roadWidth - LANE_BLOCK_THRESHOLD))
					continue;

				civUpdateDir(pCiv);

				if (pLeg->pCrosswalkNearestRoad->median_width > 0.f)
				{
					if (fDistAlongLeg > directionWidth + pLeg->pCrosswalkNearestRoad->median_width)
					{
						fDistAlongLeg -= pLeg->pCrosswalkNearestRoad->median_width;
					}
					else if (fDistAlongLeg > directionWidth)
					{	// within the median
						continue;
					}
				}

				iLaneBlock = (S32)floorf(fDistAlongLeg * invLaneWidth);
				pLeg->uLanesBlockedMask |= BIT(iLaneBlock);
				/*
				if (iLaneBlock != 0)
					iLaneBlockMin = iLaneBlock - 1;
				else
					iLaneBlockMin = iLaneBlock;

				iLaneBlockMax = iLaneBlock + 1;
				//pLeg->uLanesBlockedMask |= BIT_RANGE(iLaneBlockMin, iLaneBlockMax);
				*/

				
				if ((pLeg->uLanesBlockedMask & uAllLanesBlockedMask) == uAllLanesBlockedMask)
					break; // all lanes blocked, early out
			}
			
		} while (i--);
	}
	
	
	return false;
}


// ------------------------------------------------------------------------------------------------------------------
#define MAX_CROSSWALK_USERS		5
bool aiCivCrosswalk_CanCivUseCrosswalk(const AICivilianPathLeg *pLeg)
{
	return (s_disableCrosswalks==0) && eaSize(&pLeg->eaCrosswalkUsers) < MAX_CROSSWALK_USERS;
}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivCrosswalk_CanCivCross(const AICivilianPathLeg *pLeg)
{
	return pLeg->bIsCrosswalkOpen;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool aiCivCrosswalk_IsActive(AICivilianPathLeg *pXwalk)
{
	return !!pXwalk->uLanesBlockedMask;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool aiCivCrosswalk_AreAnyCivsWaiting(AICivilianPathLeg *pXwalk)
{
	S32 idx = eaSize(&pXwalk->eaCrosswalkUsers);
	while(--idx >= 0)
	{
		AICivCrosswalkUser *pUser = pXwalk->eaCrosswalkUsers[idx];
		if (pUser->eCrossingState == EUserCrossingState_WAITING)
			return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
AICivCrosswalkUser* aiCivCrosswalk_CreateAddUser(AICivilianPathLeg *pLeg, AICivilian *pCiv, bool bStart)
{
	S32 x, numUsers;
	U32 uFreeIndexMask = 0xFFFFFFFF;
	AICivCrosswalkUser *pNewUser = MP_ALLOC(AICivCrosswalkUser);
	
	pNewUser->bReservedAtStart = bStart;
	pNewUser->pCiv = pCiv;
	pNewUser->pCrosswalkLeg = pLeg;
	pNewUser->iPartitionIdx = pCiv->iPartitionIdx;
	
	numUsers = eaSize(&pLeg->eaCrosswalkUsers);
	for (x = 0; x < numUsers; x++)
	{
		AICivCrosswalkUser *otherUser = pLeg->eaCrosswalkUsers[x];
		
		if (otherUser->bReservedAtStart == (U32)bStart && 
			otherUser->eCrossingState != EUserCrossingState_CROSSING)
		{
			uFreeIndexMask &= ~BIT(otherUser->uReservedIndex);
		}
	}

	// try to get a random spot, if not, get the first free 
	x = randomIntRange(0, MAX_CROSSWALK_USERS - 1);
	if ( (uFreeIndexMask & BIT(x)) == 0)
	{
		x = 0;
		while(!(1 & uFreeIndexMask))
		{
			uFreeIndexMask = uFreeIndexMask >> 1;
			x++;
		}
	}

	pNewUser->uReservedIndex = x;
	eaPush(&pLeg->eaCrosswalkUsers, pNewUser);

	return pNewUser;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool aiCivCrosswalk_AnyActiveUsers(const AICivilianPathLeg *pXWalk)
{
	S32 x, numUsers;
	
	numUsers = eaSize(&pXWalk->eaCrosswalkUsers);
	for (x = 0; x < numUsers; x++)
	{
		AICivCrosswalkUser *pUser = pXWalk->eaCrosswalkUsers[x];
		if (pUser->eCrossingState == EUserCrossingState_FUTURE)
			continue;

		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCrosswalk_SetUserWaiting(AICivCrosswalkUser *pUser)
{
	AICivilianPathLeg *pXWalkLeg;
	if (pUser->eCrossingState != EUserCrossingState_FUTURE)
	{
		devassert(pUser->eCrossingState == EUserCrossingState_WAITING);
		return; 
	}

	devassert(pUser->pCrosswalkLeg);
	pXWalkLeg = pUser->pCrosswalkLeg;
	
	if (! aiCivCrosswalk_AnyActiveUsers(pXWalkLeg))
	{
		AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(pUser->iPartitionIdx);
		AICivCrosswalkManager *pCrosswalkManager;
		devassert(pPartition);
		pCrosswalkManager = pPartition->pCrosswalkManager;
		
		eaPushUnique(&pCrosswalkManager->eaActiveCrosswalks, pXWalkLeg);
	}

	pUser->eCrossingState = EUserCrossingState_WAITING;
	
	// stoplight crosswalks open and close by the traffic light logic
	if (! pXWalkLeg->bIsXingStopLight)
	{
		if (pXWalkLeg->bIsCrosswalkOpen == false) 
		{
			if (pXWalkLeg->pCrosswalkNearestRoad != NULL && pXWalkLeg->pXTrafficQuery == NULL)
			{
			#define	XTRAFFIC_QUERY_LEN	(55)
				// create two crosstraffic queries, one checking each direction of the street
				Vec3 vLegQueryPos, vQueryDir;
				F32 fQueryDist, fHalfWidth;
				F32 fQuarterLength = pXWalkLeg->len * 0.25f;

				scaleVec3(pXWalkLeg->perp, -1.f, vQueryDir);

				fHalfWidth = 0.5f * pXWalkLeg->width;
				scaleAddVec3(pXWalkLeg->dir, fQuarterLength, pXWalkLeg->start, vLegQueryPos);
				scaleAddVec3(pXWalkLeg->perp, fHalfWidth, vLegQueryPos, vLegQueryPos);
				
				pXWalkLeg->pXTrafficQuery = aiCivXTrafficCreateQuery(	pUser->iPartitionIdx, NULL, vLegQueryPos, 
																		vQueryDir, XTRAFFIC_QUERY_LEN,
																		fQuarterLength, pXWalkLeg->pCrosswalkNearestRoad, NULL);

				fQueryDist = pXWalkLeg->len * 0.75f;
				fHalfWidth = -fHalfWidth;
				scaleAddVec3(pXWalkLeg->dir, fQueryDist, pXWalkLeg->start, vLegQueryPos);
				scaleAddVec3(pXWalkLeg->perp, fHalfWidth, vLegQueryPos, vLegQueryPos);

				aiCivXTrafficCreateQuery(	pUser->iPartitionIdx, NULL, vLegQueryPos, pXWalkLeg->perp, XTRAFFIC_QUERY_LEN,
											fQuarterLength, pXWalkLeg->pCrosswalkNearestRoad, pXWalkLeg->pXTrafficQuery);

			}
			else
			{
				pXWalkLeg->bIsCrosswalkOpen = true;
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCrosswalk_SetUserCrossingStreet(AICivCrosswalkUser *pUser)
{
	devassert( pUser->eCrossingState != EUserCrossingState_FUTURE);
	pUser->eCrossingState = EUserCrossingState_CROSSING;
}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivCrosswalk_IsCrosswalkOpen(AICivCrosswalkUser *pUser)
{
	if (pUser && pUser->pCrosswalkLeg )
	{
		return pUser->pCrosswalkLeg->bIsCrosswalkOpen;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCrosswalk_SetWaypointWaitingPos(const AICivilianWaypoint *pXWalkWp, AICivilianWaypoint *pWaitingWp)
{
	#define CROSSWALK_OFFSET (5.0f)
	#define CROSSWALK_INDEX_OFFSET (4.0f)
	#define CROSSWALK_RAD_OFFSET (PI/(MAX_CROSSWALK_USERS + 1));
	
	const F32 *pvLegPos;
	F32 fDirOffset;
	Vec3 vOffset;

	devassert(pXWalkWp->pCrosswalkUser != NULL);

	if (pXWalkWp->pCrosswalkUser->bReservedAtStart)
	{
		pvLegPos = pXWalkWp->leg->start;
		fDirOffset = -CROSSWALK_OFFSET;
	}
	else 
	{
		pvLegPos = pXWalkWp->leg->end;
		fDirOffset = CROSSWALK_OFFSET;
	}

	scaleAddVec3(pXWalkWp->leg->dir, fDirOffset, pvLegPos, pWaitingWp->pos);

	fDirOffset = (F32)pXWalkWp->pCrosswalkUser->uReservedIndex * CROSSWALK_RAD_OFFSET;
	vOffset[1] = 0.0f;
	sincosf(fDirOffset, &vOffset[0], &vOffset[2]);

	scaleAddVec3(vOffset, CROSSWALK_INDEX_OFFSET, pWaitingWp->pos, pWaitingWp->pos);
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivCrosswalk_ReleaseUser(AICivCrosswalkUser **ppUser)
{
	if (*ppUser)
	{
		AICivCrosswalkUser *pCrosswalkUser = *ppUser;
		AICivilianPathLeg *pXWalkLeg = pCrosswalkUser->pCrosswalkLeg;

		eaFindAndRemoveFast(&pXWalkLeg->eaCrosswalkUsers, pCrosswalkUser);
		
		if (!aiCivCrosswalk_AnyActiveUsers(pXWalkLeg))
		{
			AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(pCrosswalkUser->iPartitionIdx);
			AICivCrosswalkManager *pCrosswalkManager;
			devassert(pPartition);
			pCrosswalkManager = pPartition->pCrosswalkManager;
						
			// make sure we reset the blocked mask
			pXWalkLeg->uLanesBlockedMask = 0;

			// no more users crossing the street, remove from the active crosswalks
			eaFindAndRemoveFast(&pCrosswalkManager->eaActiveCrosswalks, pXWalkLeg);

			if (pXWalkLeg->pXTrafficQuery)
			{
				aiCivCrossTrafficManagerReleaseQuery(&pXWalkLeg->pXTrafficQuery);
			}
		}

		MP_FREE(AICivCrosswalkUser, pCrosswalkUser);
		*ppUser = NULL;
	}
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivTraffic_DumpMissingTrafficLights(AICivilianRuntimePathInfo *pPathInfo, AICivRegenReport *report)
{
	S32 i;
		
	for (i = 0; i < eaSize(&pPathInfo->intersects); i++)
	{
		AICivilianPathIntersection *acpi = pPathInfo->intersects[i];
		S32 x;

		if (acpi->legIntersects[0]->leg->type == EAICivilianLegType_PERSON)
			continue;
		if (acpi->isectionType < EIntersectionType_marker_STOPLIGHT)
			continue;
		
		for (x = 0; x < eaSize(&acpi->legIntersects); x++)
		{
			PathLegIntersect *pli = acpi->legIntersects[x];

			FOR_EACH_IN_EARRAY(pli->eaStopLights, PathLegStopLight, pliStopLight)
				if (! IS_HANDLE_ACTIVE(pliStopLight->hStopLight))
				{
					// this leg should have a stop light, but it doesn't 
					const F32 *pvLegPos;
					Vec3 vMidPos;
					interpVec3(0.5f, acpi->max, acpi->min, vMidPos);
					if (distance3Squared(vMidPos, pli->leg->start) < distance3Squared(vMidPos, pli->leg->end))
					{
						pvLegPos = pli->leg->start;
					}
					else 
					{
						pvLegPos = pli->leg->start;
					}
					
					acgReport_AddLocation(report, pvLegPos, 
											"Road: Cannot Find Traffic Light.", 
											true, MAX(pli->leg->len, pli->leg->width));
				}
			FOR_EACH_END
		}
	}
	
}


// ------------------------------------------------------------------------------------------------------------------

void aiCivTraffic_DumpMisclassifiedIntersections(AICivilianRuntimePathInfo *pPathInfo, AICivRegenReport *report)
{
	SavedTaggedGroups *pTagData = wlLoadSavedTaggedGroupsForCurrentMap();
	S32 i;
	EArray32Handle	eaTrafficSearchTypes = NULL;

	if (! pTagData)
		return;

	ea32Push(&eaTrafficSearchTypes, ETagGleanObjectType_TRAFFIC_STOPSIGN);
	ea32Push(&eaTrafficSearchTypes, ETagGleanObjectType_TRAFFIC_LIGHT);
	
	for (i = 0; i < eaSize(&pPathInfo->intersects); i++)
	{
		Vec3 vMidPos;
		AICivilianPathIntersection *acpi = pPathInfo->intersects[i];
		F32 fSearchDist;
		TaggedObjectData *pObj;

		if (acpi->legIntersects[0]->leg->type == EAICivilianLegType_PERSON)
			continue;
		if (acpi->isectionType >= EIntersectionType_marker_STOPLIGHT)
			continue;

		interpVec3(0.5f, acpi->min, acpi->max, vMidPos);
		fSearchDist = distance3XZ(acpi->min, acpi->max) * 0.5f + 10.0f;
		
		pObj = wlTGDFindNearestObject(pTagData, vMidPos, fSearchDist, eaTrafficSearchTypes);

		if (!pObj)
		{
			// no traffic object nearby
			if (eaSize(&acpi->legIntersects) > 2)
			{
				// this intersection 
				acgReport_AddLocation(report, vMidPos, 
										"Intersection: No Traffic Object Found", false,
										distance3XZ(acpi->min, acpi->max));
			}
		}
		else if (pObj->eType == ETagGleanObjectType_TRAFFIC_LIGHT)
		{
			acgReport_AddLocation(report, vMidPos, 
									"Intersection: TrafficLight not expected", false,
									distance3XZ(acpi->min, acpi->max));
		}
		else
		{
			//ETagGleanObjectType_TRAFFIC_STOPSIGN
			if (eaSize(&acpi->legIntersects) == 2 && !acpi->bIsMidIntersection)
			{
				acgReport_AddLocation(report, vMidPos, 
										"Intersection: StopSign Ignored. Only 2 roads.", false,
										distance3XZ(acpi->min, acpi->max));
			}
		}

	}

	ea32Destroy(&eaTrafficSearchTypes);
	StructDestroySafe(parse_SavedTaggedGroups, &pTagData);
}


// ------------------------------------------------------------------------------------------------------------------
// TROLLEY Stuff
// ------------------------------------------------------------------------------------------------------------------
static void aiCivTrolley_Update(AICivilianPartitionState *pPartition)
{
	S32 i, x, count;
	AICivilianTrolley **peaTrolleyList = (AICivilianTrolley**)pPartition->civilians[EAICivilianType_TROLLEY];

	PERFINFO_AUTO_START_FUNC();

	count = eaSize(&peaTrolleyList);

	for (i = 0; i < count; i++)
	{
		AICivilianTrolley *pOuter = peaTrolleyList[i];

		for (x = i+1; x < count; x++)
		{
			AICivilianTrolley *pInner = peaTrolleyList[x];
			F32 fBlockDistSq = 0.f;
			S32 iBlockCode = checkForBlocking(&pInner->civBase, &pOuter->civBase, &fBlockDistSq);
		
			if (iBlockCode != BLOCK_CODE_NONE)
			{
				if (iBlockCode == BLOCK_CODE_FIRST)
				{
					aiCivBlock_NewBlocker(&pInner->civBase, &pInner->blockInfo, &pOuter->civBase, fBlockDistSq);
				}
				else
				{
					aiCivBlock_NewBlocker(&pOuter->civBase, &pOuter->blockInfo, &pInner->civBase, fBlockDistSq);
				}
			}
		}
	}

	PERFINFO_AUTO_STOP();
}


AUTO_COMMAND;
void aiTraffic_DisableCloseIsectForXSecs(Entity *e, F32 seconds)
{
	if (e)
	{
		Vec3 vPos;
		AICivilianPathIntersection *acpi;
		AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(entGetPartitionIdx(e));
		if (pPartition && pPartition->pIntersectionManager)
		{
			entGetPos(e, vPos);

			acpi = aiCivIntersectionManager_FindClosestStopLight(pPartition->pIntersectionManager, vPos);
			if (acpi)
			{
				Vec3 vMid;
				aiCivStopLight_DisableIntersectionForXSecs(entGetPartitionIdx(e), acpi, seconds);
				aiCivIntersection_GetBoundingMidPos(acpi, vMid);
				printf("\nDisabled intersection near (%.2f, %.2f, %.2f)\n", vMid[0], vMid[1], vMid[2]);
			}
		}
	}	
	
}

// Gets the closest interesction within an arbitrary distance and makes all lights red
// for X seconds.
AUTO_EXPR_FUNC(ai, encounter_action) ACMD_NAME(aiTrafficDisableIsectForXSecs);
void exprFuncTrafficDisableIsectForXSecs(ACMD_EXPR_PARTITION iPartitionIdx, ACMD_EXPR_LOC_MAT4_IN point, float seconds)
{
	AICivilianPathIntersection *acpi;
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(iPartitionIdx);
	
	if (pPartition && pPartition->pIntersectionManager)
	{
		acpi = aiCivIntersectionManager_FindClosestStopLight(pPartition->pIntersectionManager, point[3]);
		if (acpi)
		{
			aiCivStopLight_DisableIntersectionForXSecs(iPartitionIdx, acpi, seconds);
		}
	}
}

__forceinline static ACGCurveKey createKey(S32 legSrc, S32 srcLane, S32 legDst, S32 dstLane, S32 rightTurn)
{
	ACGCurveKey	key;
	key = legSrc | (legDst << 16) | (((U64)srcLane) << 32) | (((U64)dstLane) << 36) | (((U64)rightTurn) << 40);
	return key;
}

// ------------------------------------------------------------------------------------------------------------------
ACGCurveKey CreateKeyFromCurve(const AICivIntersectionCurve* pCurve)
{
	return createKey(pCurve->legSourceIndex, pCurve->sourceLane, pCurve->legDestIndex, pCurve->destLane, pCurve->bRightTurn);
}

// ------------------------------------------------------------------------------------------------------------------
ACGCurveKey CreateKey(const AICivilianPathLeg *pLegSrc, const AICivilianPathLeg *pLegDest, S32 srcLane, S32 dstLane, S32 bRightTurn)
{
	return createKey(pLegSrc->index, srcLane, pLegDest->index, dstLane, bRightTurn);
}
