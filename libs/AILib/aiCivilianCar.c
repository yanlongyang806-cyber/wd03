#include "aiCivilianCar.h"
#include "aiCivilian.h"
#include "aiCivilianPrivate.h"
#include "aiCivMovement.h"
#include "aiCivilianTraffic.h"

#include "Entity.h"
#include "gslMapState.h"
#include "gslMechanics.h"
#include "rand.h"
#include "timing.h"

#define CAR_INTERSECTION_UTURN_DISTFACTOR 2.0f
#define CAR_INTERSECTION_STOP_DISTFACTOR 0.75f
#define CAR_INTERMEDIATE_UTURN_DISTFACTOR (CAR_INTERSECTION_UTURN_DISTFACTOR + CAR_INTERSECTION_STOP_DISTFACTOR + 0.25f)

#define CAR_XTRAFFIC_RHT_DIST	60
#define CAR_XTRAFFIC_LHT_DIST	80

#define CAR_TIMEBEFORE_RUN_STOPSIGN 12
#define CAR_TIMEBEFORE_RUN_CROSSTRAFFIC 9

#define MIN_SPEED			15.f
#define MIN_SPEED_RANGE		6.f

F32 s_fCarTurnAngleThreshold = RAD(20.f);

static const F32 s_fCar_UpdatePerSeconds = 0.1f;

static F32 s_fCarHonkChance = 0.3f;

void aiCivCar_Initialize(Entity *e, AICivilianCar *civ, const AICivilianSpawningData *spawnData);
void aiCivCar_Free(AICivilianCar *civ);

F32 aiCivCar_GetDesiredDistFromLeg(const AICivilianCar *civ, const AICivilianWaypoint *relativeWaypt, int forward);
F32 aiCivCar_InitNextWayPtDistFromLeg(const AICivilianCar *civ, const AICivilianPathLeg *leg, AICivilianWaypoint *pNewWaypoint, int forward);
bool aiCivCar_ContinuePath(AICivilianCar *civ);
void aiCivCar_ReachedWaypoint(AICivilianCar *civ, AICivilianWaypoint *curWp);
void aiCivCar_Tick(Entity *e, AICivilianCar *civ);
void aiCivCar_ProcessPath(Entity *e, AICivilianCar *civ, const Vec3 vPos, const Vec3 vFacingDir);

bool acgLineLine2dIntersection(const Vec3 l1_pt, const Vec3 l1_dir, const Vec3 l2_pt, const Vec3 l2_dir, Vec3 vIsectPos);
__forceinline static void aiCivLeg_RemoveTrackedEnt(const AICivilianPathLeg *leg, AICivilianCar *civ);
__forceinline static void aiCivIntersection_RemoveTrackedEnt(const AICivilianPathIntersection *acpi, AICivilianCar *civ);


static AICivilianFunctionSet	s_carFunctionTable;

// ------------------------------------------------------------------------------------------------------------------
void aiCivCarInitializeData()
{
	s_carFunctionTable.CivInitialize = (fpCivInitialize) aiCivCar_Initialize;
	s_carFunctionTable.CivFree = (fpCivFree) aiCivCar_Free;
	s_carFunctionTable.CivGetDesiredDistFromLeg = (fpCivGetDesiredDistFromLeg) aiCivCar_GetDesiredDistFromLeg;
	s_carFunctionTable.CivInitNextWayPtDistFromLeg = (fpCivInitNextWayPtDistFromLeg) aiCivCar_InitNextWayPtDistFromLeg;
	s_carFunctionTable.CivContinuePath = (fpCivContinuePath) aiCivCar_ContinuePath;
	s_carFunctionTable.CivReachedWaypoint = (fpCivReachedWaypoint) aiCivCar_ReachedWaypoint;
	s_carFunctionTable.CivProcessPath = (fpCivProcessPath) aiCivCar_ProcessPath;

}


// ------------------------------------------------------------------------------------------------------------------
AICivilianFunctionSet* aiCivCarGetFunctionSet()
{
	return &s_carFunctionTable;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCar_FixupCarDef(AICivilianDef *pDef)
{
	// make sure that the speeds for the cars aren't too low
	if (pDef->fSpeedMin < MIN_SPEED)
		pDef->fSpeedMin = MIN_SPEED;
	if (pDef->fSpeedRange < MIN_SPEED_RANGE)
		pDef->fSpeedRange = MIN_SPEED_RANGE;
}

// ------------------------------------------------------------------------------------------------------------------
int aiCivCar_ValidateCarDef(AICivilianDef *pPedDef, const char *pszFilename)
{
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCar_FixupTypeDef(AICivVehicleTypeDef *pPedTypeDef)
{
	
}

// ------------------------------------------------------------------------------------------------------------------
int aiCivCar_ValidateTypeDef(AICivVehicleTypeDef *pPedTypeDef, const char *pszFilename)
{
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCar_Initialize(Entity *e, AICivilianCar *civ, const AICivilianSpawningData *spawnData) 
{
	Vec3 vBoundsMin, vBoundsMax;
	
	entGetLocalBoundingBox(e, vBoundsMin, vBoundsMax, false);

	civ->width = g_civSharedState.pMapDef->carLaneWidth;
	civ->depth = vBoundsMax[0] - vBoundsMin[0];

	aiCivBlock_Initialize(&civ->blockInfo);
	
	civ->lane = spawnData->leg->max_lanes ? randomIntRange(1, spawnData->leg->max_lanes) : 0;
		
	mmAICivilianMovementSetDesiredSpeed(CIV_BASE(civ).requester, 
										CIV_BASE(civ).civDef->fSpeedMin, 
										CIV_BASE(civ).civDef->fSpeedRange);
	
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCar_Free(AICivilianCar *civ)
{
	S32 i;

	i = eaSize(&civ->eaTrackedLegs);
	while (--i >= 0)
	{
		aiCivLeg_RemoveTrackedEnt(civ->eaTrackedLegs[i], civ);
	}

	i = eaSize(&civ->eaTrackedIntersections);
	while (--i >= 0)
	{
		aiCivIntersection_RemoveTrackedEnt(civ->eaTrackedIntersections[i], civ);
	}


	eaDestroy(&civ->eaTrackedLegs);
	eaDestroy(&civ->eaTrackedIntersections);


	if (civ->crosstraffic_request)
	{
		aiCivCrossTrafficManagerReleaseQuery(&civ->crosstraffic_request);
	}
	if (civ->stopSign_user)
	{
		aiCivStopSignUserFree(&civ->stopSign_user);
	}

}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCar_InitWaypointDefault(AICivilianCar *civ, AICivilianWaypoint *wp)
{
	wp->lane = civ->lane;
}


// ------------------------------------------------------------------------------------------------------------------
F32 aiCivCar_GetDesiredDistFromLeg(const AICivilianCar *civ, const AICivilianWaypoint *relativeWaypt, int forward)
{
	F32 lane_factor;
	F32 mult = forward ? 1 : -1;

	if (relativeWaypt->lane)
	{
		lane_factor = (relativeWaypt->lane - ((relativeWaypt->lane >= 0) ? 0.5f : -0.5f ));
	}
	else
	{
		lane_factor = 0.0f;
	}

	return (g_civSharedState.pMapDef->carLaneWidth * lane_factor + relativeWaypt->medianDist * 0.5f) * mult;
}

// ------------------------------------------------------------------------------------------------------------------
F32 aiCivCar_InitNextWayPtDistFromLeg(const AICivilianCar *civ, 
									   const AICivilianPathLeg *leg, 
									   AICivilianWaypoint *pNewWaypoint, 
									   int forward)
{
	const AICivilianWaypoint *pLastWaypoint = aiCivilianGetFromLastWaypoint(&civ->civBase, 0);
	const AICivilianWaypoint *pLastLegWp = aiCiv_LastLegWaypoint(&civ->civBase);
	F32 lane_factor;
	F32 mult = forward ? 1 : -1;

	devassert(pLastWaypoint);

	pNewWaypoint->medianDist = leg->median_width;
	
	if (leg->max_lanes)
	{// not a one way street, 

		if (pLastWaypoint->lane != 0)
		{// if we weren't in the first lane
			if (pLastWaypoint == pLastLegWp && pLastLegWp->leg->max_lanes < leg->max_lanes)
			{	// we are entering onto a street with more lanes, choose any one of them
				pNewWaypoint->lane = randomIntRange(1, leg->max_lanes);
			}
			else
			{
				pNewWaypoint->lane = CLAMP(pLastWaypoint->lane, -leg->max_lanes, leg->max_lanes);
			}
		}
		else
		{
			// coming off a one-lane street
			pNewWaypoint->lane = randomIntRange(1, leg->max_lanes);
		}

		lane_factor = (pNewWaypoint->lane - ((pNewWaypoint->lane >= 0) ? 0.5f : -0.5f ));
	}
	else
	{
		pNewWaypoint->lane = 0;
		lane_factor = 0.0f;
	}

	return (g_civSharedState.pMapDef->carLaneWidth * lane_factor + pNewWaypoint->medianDist/2)*mult;
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivCar_Tick(Entity *e, AICivilianCar *civ)
{
	Vec3 vFacingDir;
	Vec3 vPos;

	PERFINFO_AUTO_START("car", 1);

	// invalidate the civilian cached direction and position
	// only if we're not currently blocked
	if (civ->blockInfo.eBlockType == EBlockType_NONE)
	{
		civ->civBase.v2DirIsCurrent = false;
		civ->civBase.v2PosIsCurrent = false;
	}
	
	if(ABS_TIME_SINCE_PARTITION(civ->civBase.iPartitionIdx, civ->civBase.lastUpdateTime) < SEC_TO_ABS_TIME(s_fCar_UpdatePerSeconds))
	{
		// update
		PERFINFO_AUTO_STOP();
		return;
	}

	entGetPos(e, vPos);

	if(FALSE_THEN_SET(civ->civBase.v2PosIsCurrent))
	{
		civCopyVec3ToVec2(vPos, civ->civBase.v2Pos);
	}

	{
		Vec2 v2PitchYaw;
		entGetFacePY(e, v2PitchYaw);
		sincosf(v2PitchYaw[1], &vFacingDir[0], &vFacingDir[2]);
		vFacingDir[1] = 0.0f;
	}
	

	if(FALSE_THEN_SET(civ->civBase.v2DirIsCurrent))
	{
		civCopyVec3ToVec2(vFacingDir, civ->civBase.v2Dir);
	}

	civ->civBase.lastUpdateTime = ABS_TIME_PARTITION(civ->civBase.iPartitionIdx);
	aiCivilianProcessPath(e, &civ->civBase, vPos, vFacingDir);

	PERFINFO_AUTO_STOP();
}


// ------------------------------------------------------------------------------------------------------------------
#define CAR_BOUNDING_SIZE	(20.0f)
// at the moment, intersection bounding is not oriented, as well as the cars bounding
__forceinline static bool isWithinIntersection(const AICivilianPathIntersection *acpi, const Vec3 vPos)
{
	if (acpi->max[0] < (vPos[0] - CAR_BOUNDING_SIZE) || (vPos[0] + CAR_BOUNDING_SIZE) < acpi->min[0])
		return false;
	if (acpi->max[2] < (vPos[2] - CAR_BOUNDING_SIZE) || (vPos[2] + CAR_BOUNDING_SIZE) < acpi->min[2])
		return false;

	return true;
}

// ------------------------------------------------------------------------------------------------------------------
#define CAR_LEN_THRESHOLD		(50.0f)
#define CAR_WIDTH_THRESHOLD		(8.0f)
// returns true if the position is within the thresholds of the leg
__forceinline static bool isNearLeg(const AICivilianPathLeg *leg, const Vec3 vPos)
{
	Vec3 diff;
	F32 fParallelDist, fPerpendicularDist;
	subVec3(vPos, leg->start, diff);
	fParallelDist = dotVec3(diff, leg->dir);
	fPerpendicularDist = dotVec3(diff, leg->perp);

	return (fParallelDist >= -CAR_LEN_THRESHOLD) && (fParallelDist <= leg->len + CAR_LEN_THRESHOLD) &&
		(ABS(fPerpendicularDist) - CAR_WIDTH_THRESHOLD <= leg->width * 0.5f);
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivLeg_AddTrackedCiv(const AICivilianPathLeg *leg, AICivilianCar *civ)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(CIV_BASE(civ).iPartitionIdx);
	eaPush(&civ->eaTrackedLegs, leg);

	stashIntAddPointer(leg->tracked_ents, civ->civBase.myEntRef, civ, 1);


	aiCivCarBlockManager_ReportAddedLeg(pPartition->pCarBlockManager, (AICivilianPathLeg*)leg);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivLeg_RemoveTrackedEnt(const AICivilianPathLeg *leg, AICivilianCar *civ)
{
	eaFindAndRemoveFast(&civ->eaTrackedLegs, leg);

	stashIntRemovePointer(leg->tracked_ents, civ->civBase.myEntRef, NULL);

	aiCivCarBlockManager_ReportRemovedLeg((AICivilianPathLeg*)leg);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivIntersection_AddTrackedEnt(const AICivilianPathIntersection *acpi, AICivilianCar *civ)
{
	AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(CIV_BASE(civ).iPartitionIdx);
	eaPush(&civ->eaTrackedIntersections, acpi);

	stashIntAddPointer(acpi->tracked_ents, civ->civBase.myEntRef, civ, 1);

	aiCivCarBlockManager_ReportAddedIntersection(pPartition->pCarBlockManager, (AICivilianPathIntersection*)acpi);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivIntersection_RemoveTrackedEnt(const AICivilianPathIntersection *acpi, AICivilianCar *civ)
{
	eaFindAndRemoveFast(&civ->eaTrackedIntersections, acpi);

	stashIntRemovePointer(acpi->tracked_ents, civ->civBase.myEntRef, NULL);

	aiCivCarBlockManager_ReportRemovedIntersection((AICivilianPathIntersection*)acpi);
}

// ------------------------------------------------------------------------------------------------------------------
static void civCarAddLegForTrackingIfNeeded(AICivilianCar *civ, const Vec3 vPos, const AICivilianPathLeg *leg)
{
	if (eaFind(&civ->eaTrackedLegs, leg) == -1)
	{
		if (isNearLeg(leg, vPos))
		{
			aiCivLeg_AddTrackedCiv(leg, civ);
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void civCarAddIntersectionForTrackingIfNeeded(AICivilianCar *civ, const Vec3 vPos, const AICivilianPathIntersection *acpi)
{
	if (eaFind(&civ->eaTrackedIntersections, acpi) == -1)
	{
		if (isWithinIntersection(acpi, vPos))
		{
			aiCivIntersection_AddTrackedEnt(acpi, civ);
		}
	}
}


// ------------------------------------------------------------------------------------------------------------------
#define BOUNDING_THRESHOLD_FACTOR 2.0f

static void civCarUpdateIntersectionTracking(Entity *e, AICivilianCar *civ, const Vec3 vPos, const Vec2 v2Dir)
{
	static const AICivilianPathIntersection **s_remIntersections = NULL;
	AICivilianWaypoint *curWp;
	S32 i;


	// Update the currently tracked legs
	i = eaSize(&civ->eaTrackedIntersections);
	while(--i >= 0)
	{
		const AICivilianPathIntersection *acpi = civ->eaTrackedIntersections[i];

		if( ! isWithinIntersection(acpi, vPos) )
		{
			// we are too far from the leg, flag it for removal
			eaPush(&s_remIntersections, acpi);
		}
	}

	curWp = aiCivilianGetCurrentWaypoint(&civ->civBase);
	// check our current waypoint
	if (curWp->bIsLeg)
	{
		const AICivilianPathLeg *curLeg = curWp->leg;

		if (curLeg->nextInt)
			civCarAddIntersectionForTrackingIfNeeded(civ, vPos, curLeg->nextInt);
		if (curLeg->prevInt)
			civCarAddIntersectionForTrackingIfNeeded(civ, vPos, curLeg->prevInt);

		i = eaSize(&curLeg->midInts);
		while(--i >= 0)
		{
			AICivilianPathIntersection *acpi = curLeg->midInts[i];
			civCarAddIntersectionForTrackingIfNeeded(civ, vPos, acpi);
		}
	}
	else
	{
		civCarAddIntersectionForTrackingIfNeeded(civ, vPos, curWp->acpi);
	}

	// free all the EntLegPos flagged for removal
	i = eaSize(&s_remIntersections);
	if (i)
	{
		i--;
		do {
			aiCivIntersection_RemoveTrackedEnt(s_remIntersections[i], civ);
		} while (--i >= 0);

		eaClear(&s_remIntersections);
	}


}

// ------------------------------------------------------------------------------------------------------------------
static void civCarUpdateLegTracking(Entity *e, AICivilianCar *civ, const Vec3 vPos, const Vec2 v2Dir)
{
	static const AICivilianPathLeg **s_remLegs = NULL;
	AICivilianWaypoint *curWp;
	S32 i;


	// Update the currently tracked legs
	i = eaSize(&civ->eaTrackedLegs);
	while(--i >= 0)
	{
		const AICivilianPathLeg *leg = civ->eaTrackedLegs[i];
		if( ! isNearLeg(leg, vPos) )
		{
			eaPush(&s_remLegs, leg);
		}
	}

	curWp = aiCivilianGetCurrentWaypoint(&civ->civBase);

	// check our current waypoint's legs and see if they need to be added
	if (curWp->bIsLeg)
	{
		const AICivilianPathLeg *curLeg = curWp->leg;

		civCarAddLegForTrackingIfNeeded(civ, vPos, curLeg);

		if (curLeg->prev)
			civCarAddLegForTrackingIfNeeded(civ, vPos, curLeg->prev);
		if (curLeg->next)
			civCarAddLegForTrackingIfNeeded(civ, vPos, curLeg->next);

		if (curWp->car_move_type >= EAICivCarMove_GENERIC_TURN)
		{
			if (civ->civBase.path.curWp + 3 < eaSize(&civ->civBase.path.eaWaypoints))
			{
				ANALYSIS_ASSUME(civ->civBase.path.eaWaypoints);
				curWp = civ->civBase.path.eaWaypoints[civ->civBase.path.curWp + 3];
				if (curWp->bIsLeg)
				{
					civCarAddLegForTrackingIfNeeded(civ, vPos, curWp->leg);
				}
			}
		}
	}
	else
	{
		// test to add all the intersection's legs
		for (i = 0; i < eaSize(&curWp->acpi->legIntersects); i++)
		{
			PathLegIntersect *pli = curWp->acpi->legIntersects[i];
			civCarAddLegForTrackingIfNeeded(civ, vPos, pli->leg);
		}
	}

	// free all the EntLegPos flagged for removal
	i = eaSize(&s_remLegs);
	if (i)
	{
		i--;
		do {
			aiCivLeg_RemoveTrackedEnt(s_remLegs[i], civ);
		} while(--i >= 0);

		eaClear(&s_remLegs);
	}

}

// ------------------------------------------------------------------------------------------------------------------
// the waypoint is assumed to be on the list, if the wp is not found this function will assert!
// note: the passed in wayPoint will be included in the calculation
__forceinline static int aiCivCalculateFutureForwardToWayPoint(const AICivilian *civ, const AICivilianWaypoint *wp)
{
	int forward = civ->forward;
	int i;

	// Check future points for reversals
	ANALYSIS_ASSUME(civ->path.eaWaypoints);
	for(i=civ->path.curWp + 1; i < eaSize(&civ->path.eaWaypoints); i++)
	{
		AICivilianWaypoint *fw = civ->path.eaWaypoints[i];
		if(fw->bReverse)
			forward = !forward;
		if (fw == wp)
			return forward;
	}

	devassertmsg(0, "AICivilian could not find waypoint in list for calculating the future forward boolean!");
	return forward;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static const F32* aiCivGetMidIntersectionPos(const AICivilianPathIntersection *acpi)
{
	devassert(acpi->bIsMidIntersection);
	devassert( eaSize(&acpi->legIntersects) == MAX_PATHLEG_MID_INTERSECTIONS);
	if (vec3IsZero(acpi->legIntersects[0]->intersect))
	{
		return acpi->legIntersects[1]->intersect;
	}
	else
	{
		return acpi->legIntersects[0]->intersect;
	}
}


// ------------------------------------------------------------------------------------------------------------------
static void civCarRequestCrossTraffic_Mid_SideToEdge(AICivilianCar *civ, const AICivilianWaypoint *nextWp, F32 length, bool bOpposite)
{
	AICivilianTrafficQuery *new_query;
	const AICivilianWaypoint *nextLegWp = aiCivilianGetNextLegWaypoint(&civ->civBase);
	const AICivilianPathLeg *destLeg;
	Vec3 vXTrafficCheckDir;
	Vec3 vXTrafficCheckPos;
	F32 half_width;
	int future_forward;

	devassert(nextLegWp);

	future_forward = aiCivCalculateFutureForwardToWayPoint(&civ->civBase, nextLegWp) ^ bOpposite;

	destLeg = nextLegWp->leg;
	// todo: only get the width needed for the lanes we are crossing.
	// right now it is going to check every lane
	half_width = destLeg->lane_width * 0.5 * destLeg->max_lanes;

	// get the position for the check
	{
		const F32 *pIsectPos = aiCivGetMidIntersectionPos(nextWp->acpi);
		F32 fPosOffset = (future_forward) ? half_width : -half_width;
		scaleAddVec3(destLeg->perp, fPosOffset, pIsectPos, vXTrafficCheckPos);
	}
	// get the direction of the check
	{
		if (future_forward)
		{// check in the opposite direction of the forward
			setVec3(vXTrafficCheckDir, -destLeg->dir[0], -destLeg->dir[1], -destLeg->dir[2]);
		}
		else
		{
			copyVec3(destLeg->dir, vXTrafficCheckDir);
		}
	}

	new_query = aiCivXTrafficCreateQuery(	CIV_BASE(civ).iPartitionIdx, &civ->civBase, vXTrafficCheckPos, vXTrafficCheckDir,
											length, half_width, destLeg, civ->crosstraffic_request);
	if (civ->crosstraffic_request == NULL)
	{
		civ->crosstraffic_request = new_query;
	}
}

// ------------------------------------------------------------------------------------------------------------------
static void civCarRequestCrossTraffic_Mid_EdgeToSide(AICivilianCar *civ, const AICivilianWaypoint *curWp, const AICivilianWaypoint *nextWp)
{
	//const AICivilianWaypoint *nextNextWp = aiCivilianGetNextNextWaypoint(civ);
	const AICivilianPathLeg *curLeg;
	Vec3 vXTrafficCheckDir;
	Vec3 vXTrafficCheckPos;
	F32 half_width;
	// int future_forward = aiCivCalculateFutureForwardToWayPoint(civ, nextNextWp);

	curLeg = curWp->leg;
	// todo: only get the width needed for the lanes we are crossing.
	// right now it is going to check every lane
	half_width = curLeg->lane_width * 0.5 * curLeg->max_lanes;

	// get the position for the check
	{
		const F32 *pIsectPos = aiCivGetMidIntersectionPos(nextWp->acpi);
		F32 fPosOffset = (civ->civBase.forward) ? -half_width : half_width;
		scaleAddVec3(curLeg->perp, fPosOffset, pIsectPos, vXTrafficCheckPos);
	}
	// get the direction of the check
	{
		if (civ->civBase.forward)
		{
			copyVec3(curLeg->dir, vXTrafficCheckDir);
		}
		else
		{
			setVec3(vXTrafficCheckDir, -curLeg->dir[0], -curLeg->dir[1], -curLeg->dir[2]);
		}
	}

	civ->crosstraffic_request = aiCivXTrafficCreateQuery(	CIV_BASE(civ).iPartitionIdx, 
															&civ->civBase, vXTrafficCheckPos, vXTrafficCheckDir,
															CAR_XTRAFFIC_LHT_DIST, half_width, curLeg, NULL );
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCivGetNextLegAndPrevIsectWaypoint(AICivilian *civ, const AICivilianWaypoint **pPrevAcpi,
															  const AICivilianWaypoint **pNextLeg)
{
	S32 i, size;

	ANALYSIS_ASSUME(civ->path.eaWaypoints);
	size = eaSize(&civ->path.eaWaypoints);
	i = civ->path.curWp + 1;
	do {
		AICivilianWaypoint *wp = civ->path.eaWaypoints[i];
		if (wp->bIsLeg)
		{
			*pNextLeg = wp;
			*pPrevAcpi = civ->path.eaWaypoints[i - 1];
			devassert(!(*pPrevAcpi)->bIsLeg);
			return;
		}

	} while (++i < size);

	devassert(0);
	return;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCar_ProcessPath(Entity *e, AICivilianCar *civ, const Vec3 vPos, const Vec3 vFacingDir)
{
	AICivilianWaypoint *curWp = NULL;
	Vec2 v2Dir;

	civCopyVec3ToVec2(vFacingDir, v2Dir);
	normalVec2(v2Dir);
	copyVec2(v2Dir, civ->civBase.v2Dir);

	// Check for blockages for oriented folks
	curWp = aiCivilianGetCurrentWaypoint(&civ->civBase);

	civCarUpdateIntersectionTracking(e, civ, vPos, v2Dir);
	civCarUpdateLegTracking(e, civ, vPos, v2Dir);

	if (civ->blockInfo.eBlockType > EBlockType_NONE)
	{
		bool bIsBlocked;
		EBlockType blockType;
		blockType = aiCivBlock_UpdateBlocking(&civ->civBase, &civ->blockInfo);

		bIsBlocked = (EBlockType_NONE != blockType);

		// Reroute around if possible
		if(bIsBlocked)
		{
			mmAICivilianMovementSetDesiredSpeed(civ->civBase.requester, 0.0f, 0.0f);

			if (! civ->blockedTime)
			{
				civ->blockedTime = timeSecondsSince2000();
				
				if (g_civSharedState.pMapDef->car.pchCarHonkSoundName && randomChance(s_fCarHonkChance))
				{
					mechanics_playOneShotSoundAtLocation(entGetPartitionIdx(e), vPos, NULL, g_civSharedState.pMapDef->car.pchCarHonkSoundName, __FILE__);
				}
			}
			else
			{
				U32 blockedSeconds = timeSecondsSince2000() - civ->blockedTime;
				int breakhere = 0;
			}
			return;
		}
	}

	civ->blockedTime = 0;
	
	mmAICivilianMovementSetDesiredSpeed(CIV_BASE(civ).requester, 
										CIV_BASE(civ).civDef->fSpeedMin, 
										CIV_BASE(civ).civDef->fSpeedRange);

	// check if we are stopped before an intersection
	if (curWp->bIsLeg && curWp->bStop)
	{
		AICivilianWaypoint *nextWp = aiCivilianGetNextWaypoint(&civ->civBase);
		devassert(! nextWp->bIsLeg); // if we're at a stop the next waypoint needs to be an intersection

		if (nextWp->acpi->isectionType >= EIntersectionType_marker_STOPLIGHT)
		{
			// check our next wp to see if the stoplight is telling me to go yet.
			if (aiCivStopLight_IsWayGreen(nextWp->acpi, curWp->leg))
			{
				curWp->bStop = false;
			}
		}
		else
		{	// stop sign
			U32 stoppedSeconds;
			if (civ->stopSignTime != 0)
			{
				stoppedSeconds = timeSecondsSince2000() - civ->stopSignTime;
			}
			else
			{
				civ->stopSignTime = timeSecondsSince2000();
				stoppedSeconds = 0;
			}

			if (nextWp->acpi->isectionType == EIntersectionType_SIDESTREET_STOPSIGN)
			{
				// cross traffic handling
				if (civ->crosstraffic_request == NULL)
				{
					if (curWp->car_street_isect_type == EAICivCarStreetIsect_MID_SIDETOEDGE)
					{
						if (curWp->car_move_type == EAICivCarMove_RIGHT_HAND_TURN)
						{
							// check traffic in the lane we wish to merge into
							civCarRequestCrossTraffic_Mid_SideToEdge(civ, nextWp, CAR_XTRAFFIC_RHT_DIST, false);
						}
						else
						{
							devassert(curWp->car_move_type == EAICivCarMove_LEFT_HAND_TURN);
							// check traffic heading from the left
							civCarRequestCrossTraffic_Mid_SideToEdge(civ, nextWp, CAR_XTRAFFIC_LHT_DIST, false);
							// and the traffic we will be merging into
							civCarRequestCrossTraffic_Mid_SideToEdge(civ, nextWp, CAR_XTRAFFIC_RHT_DIST, true);
						}
					}
					else if (curWp->car_street_isect_type == EAICivCarStreetIsect_MID_EDGETOSIDE)
					{
						civCarRequestCrossTraffic_Mid_EdgeToSide(civ, curWp, nextWp);
					}
				}

				if (!civ->crosstraffic_request || aiCivXTrafficQueryIsSatisfied(civ->crosstraffic_request) ||
					stoppedSeconds > CAR_TIMEBEFORE_RUN_CROSSTRAFFIC )
				{
					curWp->bStop = false;

					civ->stopSignTime = 0;
					aiCivCrossTrafficManagerReleaseQuery(&civ->crosstraffic_request);
				}
			}
			else
			{
				bool bPathClear;
				const AICivilianWaypoint *nextLegWp = NULL;
				const AICivilianWaypoint *lastAcpiWp = NULL;

				aiCivGetNextLegAndPrevIsectWaypoint(&civ->civBase, &lastAcpiWp, &nextLegWp);

				bPathClear = aiCivStopSignIsWayClear(nextWp->acpi, &civ->civBase, curWp->pos, lastAcpiWp->pos, nextLegWp->leg, curWp->leg);
				if (bPathClear || stoppedSeconds > CAR_TIMEBEFORE_RUN_STOPSIGN)
				{
					AICivilianPathIntersection *acpi = (AICivilianPathIntersection*)nextWp->acpi;

					aiCivStopSignUserAdd((AICivilianPathIntersection*)nextWp->acpi, civ,
											curWp->pos, lastAcpiWp->pos, nextLegWp->leg, curWp->leg);

					curWp->bStop = false;

					civ->stopSignTime = 0;
				}
			}
		}
	}
	else
	{
		AICivilianWaypoint *nextWp;

		// check if the next waypoint is a stop, and check if the intersection type is a stop light
		// if it is, set the stop flag depending if the light is telling us to go
		nextWp = aiCivilianGetNextWaypoint(&civ->civBase);
		if (nextWp && nextWp->bIsLeg && nextWp->bWasStop)
		{
			AICivilianWaypoint *nextNextWp;

			nextNextWp = aiCivilianGetNextNextWaypoint(&civ->civBase);
			if (! nextNextWp->bIsLeg && (nextNextWp->acpi->isectionType >= EIntersectionType_marker_STOPLIGHT) )
			{
				nextWp->bStop = ! aiCivStopLight_IsWayGreen(nextNextWp->acpi, nextWp->leg);
			}
		}

	}

	aiCivilianCheckReachedWpAndContinue(e, &civ->civBase, vPos);
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivCar_ReachedWaypoint(AICivilianCar *civ, AICivilianWaypoint *curWp)
{
	if (civ->stopSign_user)
	{
		AICivilianWaypoint *nextWp = aiCivilianGetNextWaypoint(&civ->civBase);
		if (nextWp->bIsLeg)
		{
			aiCivStopSignUserFree(&civ->stopSign_user);
		}
	}
	/*
	// leaving this curWp, check if we are leaving the stopSign intersection
	if (!curWp->bIsLeg && civ->stopSign_user && aiCivStopSignUserCheckACPI(civ->stopSign_user, curWp->acpi))
	{
		// queue this to be removed, but we need to make sure that we don't have another
		// waypoint that is still using this stop sign intersection
		bRemoveStopSignUser = true;
	}
	//
	*/
	civ->lane = curWp->lane;

}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static EAICivCarMove civCarClassifyTurn(const Vec3 curLegDir, const Vec3 nextLegDir, bool reverse)
{
	if ( (curLegDir[2]*nextLegDir[0] - curLegDir[0]*nextLegDir[2] > 0.0f) ^ reverse)
	{
		return EAICivCarMove_RIGHT_HAND_TURN;
	}

	return EAICivCarMove_LEFT_HAND_TURN;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static EAICivCarMove civCarClassifyMove(const Vec3 curLegDir, const Vec3 nextLegDir, bool reverse)
{
	static const F32 s_fStraightThreshold = 0.766f; // ~ cosf(40.f);
	F32 fDot = dotVec3XZ(curLegDir, nextLegDir);

	if (ABS(fDot) >= s_fStraightThreshold)
	{
		return EAICivCarMove_STRAIGHT;
	}

	return civCarClassifyTurn(curLegDir, nextLegDir, reverse);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static bool isValidFlowOntoOneWay(const AICivilianPathLeg *sourceLeg, const AICivilianPathLeg *destLeg, bool forward)
{
	// assumes destLeg is a one-way street, we can only go down it if
	// we're already heading in this direction.
	bool bReverse = !calcLegFlow(sourceLeg, destLeg);
	return bReverse ^ forward; 
}

// ------------------------------------------------------------------------------------------------------------------
bool carValidatePathLegMidIntersections(const AICivilian *civ, 
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

		if (isect_pli->leg->bIsOneWay && ! isValidFlowOntoOneWay(wp->leg, isect_pli->leg, forward))
		{
			return false;
		}

		if (prev_wp->acpi == acpi) // (unnessesary to check this, the pointer compare alone should be fine) !prev_wp->bIsLeg && 
		{
			// don't take the same mid intersection twice!
			return false;
		}

		// this mid intersection is somewhere on the middle of the leg.
		// make sure that the mid intersection is ahead, and not behind us.
		subVec3(isect_pli->intersect, prev_wp->pos, vWayPtToIntersect);
		fDot = dotVec3(vWayPtToIntersect, wp->leg->dir);
		if ( (forward && fDot < 0) || (!forward && fDot > 0))
		{
			// this waypoint is 'behind' us, we cannot use it
			return false;
		}

		if (wp->leg == leg_pli->leg && 
			(wp->leg->max_lanes > 1 || leg_pli->leg->median_width > 0.f))
		{
			int flow = 0, bReverse;
			EAICivCarMove carMoveType;

			stashAddressFindInt(leg_pli->leg->flowStash, isect_pli->leg, &flow);
			bReverse = (flow != forward);

			carMoveType = civCarClassifyTurn(wp->leg->dir, isect_pli->leg->dir, bReverse);
			if (carMoveType == EAICivCarMove_RIGHT_HAND_TURN)
			{
				if (wp->lane != wp->leg->max_lanes)
					return false;
			}
			else
			{
				// not in the right lane, or the road has a median (which cannot be driven over)
				if (wp->lane != 1 || leg_pli->leg->median_width > 0.f)
					return false;
			}
		}
		
		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static bool continueValidateLegIntersections(const AICivilianWaypoint *wp, const AICivilianPathIntersection *acpi, bool forward)
{
	bool bValidated = false;
	S32 i = eaSize(&acpi->legIntersects) - 1;

if (acpi->bIsMidIntersection == true)
	{
		devassert (acpi->legIntersects);
		do {
			PathLegIntersect *pli = acpi->legIntersects[i];
			pli->continue_isvalid = true;
		} while(--i >= 0);

		bValidated = true;
	}
	else
	{
		EAICivCarMove	eDesiredCarMove;


		if (wp->leg->max_lanes <= 1)
		{
			eDesiredCarMove = EAICivCarMove_COUNT;
		}
		else
		{
			eDesiredCarMove = (wp->lane == 1) ? EAICivCarMove_LEFT_HAND_TURN : EAICivCarMove_RIGHT_HAND_TURN;
		}

		devassert (acpi->legIntersects);
		do {

			PathLegIntersect *pli = acpi->legIntersects[i];

			if (pli->leg == wp->leg)
			{
				pli->continue_isvalid = false;
				continue;
			}

			if (eDesiredCarMove != EAICivCarMove_COUNT)
			{
				bool bReverse = !calcLegFlow(wp->leg, pli->leg);
				EAICivCarMove eCarMove = civCarClassifyMove(wp->leg->dir, pli->leg->dir, bReverse);
				if (eCarMove != EAICivCarMove_STRAIGHT && eCarMove != eDesiredCarMove)
				{
					pli->continue_isvalid = false;
					continue;
				}
			}

			if (pli->leg->bIsOneWay && ! isValidFlowOntoOneWay(wp->leg, pli->leg, forward))
			{
				pli->continue_isvalid = false;
				continue;
			}

			pli->continue_isvalid = true;
			bValidated = true;

		} while(--i >= 0);
	}



	return bValidated;
}

// ------------------------------------------------------------------------------------------------------------------
// returns the stop waypoint
static AICivilianWaypoint* civCarInsertStopWaypoint(AICivilianCar *civ, AICivilianWaypoint *wp, 
													const AICivilianWaypoint *nextWp, bool forward)
{
	AICivilianWaypoint *stopWp;

	// we will be moving our current waypoint to stop just before the intersection
	// and put a copy of the old waypoint after it
	stopWp = aiCivilianWaypointAlloc();
	memcpy(stopWp, wp, sizeof(AICivilianWaypoint));
	stopWp->bReverse = false;
	stopWp->bStop = true;
	stopWp->bWasStop = true;

	//
	aiCiv_AddNewWaypointToPath(&civ->civBase.path, stopWp);

	// now move our current waypoint back to stop before the intersection
	{

		F32 stopDistance = forward ? -civ->width : civ->width;
		stopDistance *= CAR_INTERSECTION_STOP_DISTFACTOR;

		scaleAddVec3(wp->leg->dir, stopDistance, wp->pos, wp->pos);
	}

	return stopWp;
}

// ------------------------------------------------------------------------------------------------------------------
static const AICivilianPathLeg* aiCivilianACPIGetRandomLeg(const AICivilianPathIntersection *acpi, const AICivilianPathLeg *pLegIgnore)
{
	PathLegIntersect *pli = acpi->legIntersects[randomIntRange(0, eaSize(&acpi->legIntersects)-1)];
	if (pli->continue_isvalid && pli->leg != pLegIgnore)
	{
		return pli->leg;
	}

	// find a random leg of the ones available
	{
		S32 x, count = 0;
		x = eaSize(&acpi->legIntersects);
		devassert(x > 0);
		do {
			x--;
			if (acpi->legIntersects[x]->continue_isvalid && acpi->legIntersects[x]->leg != pLegIgnore)
			{
				count ++;
			}

		} while(x > 0);

		devassert(count > 0);
		count = randomIntRange(0, count-1);
		x = 0;
		do {
			if (acpi->legIntersects[x]->continue_isvalid && acpi->legIntersects[x]->leg != pLegIgnore)
			{
				if (count == 0)
				{
					return acpi->legIntersects[x]->leg;
				}
				count--;
			}
			x++;
		} while (1);
	}

	devassert(0);

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivCar_ContinuePath(AICivilianCar *civ)
{
	static AICivilianPathIntersection **s_validMids = NULL;

	AICivPathSelectionInput		pathSel;
	S32							future_forward;
	AICivilianWaypoint			*nextWp;
	AICivilianWaypoint			*wp = aiCivilianGetFromLastWaypoint(&civ->civBase, 0);

	devassert(wp);

	// we can only continue the path if the current way point is on a leg
	if(! wp->bIsLeg)
		return false;

	future_forward  = aiCivilianCalculateFutureForward(&civ->civBase);
	pathSel.pLeg = future_forward  ? wp->leg->next : wp->leg->prev;
	pathSel.pAcpi = future_forward  ? wp->leg->nextInt : wp->leg->prevInt;
	pathSel.eaMids = &s_validMids;

	eaClear(&s_validMids);

	// validate and build a list of all the possible connections that we can get to
	{
		AICivilianPathIntersection **mids = wp->leg->midInts;
		AICivilianWaypoint *prevWp = aiCivilianGetFromLastWaypoint(&civ->civBase, 1);

		if (pathSel.pLeg)
		{	// validate our leg
			if (pathSel.pLeg->bIsOneWay && ! isValidFlowOntoOneWay(wp->leg, pathSel.pLeg, future_forward))
			{
				// (I don't think a non-one-way leg connecting to a one-way leg is
				// a very valid configuration. I can't rule it out, though)
				pathSel.pLeg = NULL;
			}
		}

		if (pathSel.pAcpi)
		{	// validate our intersection connections
			if (!continueValidateLegIntersections(wp, pathSel.pAcpi, future_forward))
			{
				pathSel.pAcpi = NULL;
			}
		}

		if (mids)
		{	// validate our mid-intersections
			S32 i;

			i = eaSize(&mids);
			while(--i >= 0)
			{
				AICivilianPathIntersection *midACPI = mids[i];
				if (carValidatePathLegMidIntersections(&civ->civBase, wp, prevWp, midACPI, future_forward))
				{
					eaPush(&s_validMids, midACPI);
				}
			}
		}
	}

	nextWp = aiCivilianWaypointAlloc();

	if(!pathSel.pLeg && !pathSel.pAcpi && !eaSize(pathSel.eaMids))
	{
		// no further place to go, we have to turn around.
		if (wp->leg->bIsOneWay == false)
		{

			F32 fUTurnShellParaDist;
			// cars performing a u-turn
			// make a u-turn shell for the movement to curve

			fUTurnShellParaDist = CAR_INTERSECTION_UTURN_DISTFACTOR * civ->width;
			if (wp->leg->len < fUTurnShellParaDist)
			{
				fUTurnShellParaDist = wp->leg->len - 1.0f;
			}

			if (future_forward)
				fUTurnShellParaDist = -fUTurnShellParaDist;

			// copy the current waypoint
			{
				AICivilianWaypoint *intermediateWp = aiCivilianWaypointAlloc();
				intermediateWp->bIsLeg = true;
				intermediateWp->leg = wp->leg;
				copyVec3(wp->pos, intermediateWp->pos);
				intermediateWp->bIsUTurn = true;
				aiCiv_AddNewWaypointToPath(&civ->civBase.path, intermediateWp);
			}

			{
				// move the wp back
				// (todo: need to make sure that we're not moving this past a previous waypoint)
				scaleAddVec3(wp->leg->dir, fUTurnShellParaDist, wp->pos, wp->pos);

				wp->bIsUTurn = true;
			}

			{
				nextWp->bIsLeg = true;
				nextWp->leg = wp->leg;
				nextWp->bIsUTurn = true;
				nextWp->bReverse = true;
				aiCivilianMakeWpPos(&civ->civBase, nextWp->leg, nextWp, 0);
				aiCiv_AddNewWaypointToPath(&civ->civBase.path, nextWp);
			}

			{
				AICivilianWaypoint *legWp = aiCivilianWaypointAlloc();

				legWp->bIsLeg = true;
				legWp->leg = wp->leg;
				aiCivilianMakeWpPos(&civ->civBase, legWp->leg, legWp, 1);

				if (wp->leg->len > (civ->width * CAR_INTERMEDIATE_UTURN_DISTFACTOR) &&
					distance3SquaredXZ(nextWp->pos, legWp->pos) > SQR(fUTurnShellParaDist))
				{// we need to insert a position to keep the shell short
					AICivilianWaypoint *intermediateWp = aiCivilianWaypointAlloc();

					intermediateWp->bIsLeg = true;
					intermediateWp->leg = nextWp->leg;
					scaleAddVec3(nextWp->leg->dir, fUTurnShellParaDist, nextWp->pos, intermediateWp->pos);
					aiCiv_AddNewWaypointToPath(&civ->civBase.path, intermediateWp);
				}

				aiCiv_AddNewWaypointToPath(&civ->civBase.path, legWp);
			}

			return true;
		}
		else
		{
			// rrp 2.4.09
			// we're at the end of a one-way street.
			// this is a special case that won't ever be hit once the leg generation
			// removes all dead-end one-way streets.
			aiCivilianWaypointFree(nextWp);
			civ->civBase.flaggedForKill = 1;
			return false;
		}
	}
	else
	{
	
		{
			AICivPathSelectionOutput output;
			// from our available choices, choose a random leg or intersection to traverse
			civContinuePath_GetRandomPath(&pathSel, &output);
			//

			if (output.pLeg)
			{
				nextWp->bIsLeg = true;
				nextWp->leg = output.pLeg;
			}
			else
			{
				devassert(output.pAcpi != NULL);
				nextWp->bIsLeg = false;
				nextWp->acpi = output.pAcpi;
			}
		}


		if(nextWp->bIsLeg)
		{
			// 2.2.09
			// check the angle between the wp and the next,
			// if the angle is greater than our threshold, then we need
			// to modify the waypoint to ensure we do not cross into the next leg
			// (this is most obvious on legs that are at a 90degree angle)
			F32 fDot;

			ANALYSIS_ASSUME(nextWp->leg);
			fDot = dotVec3(wp->leg->dir, nextWp->leg->dir);
			if (fabs(fDot) <= cosf(RAD(30.0f)))
			{
				Vec3 pt2, vIsectPt;
				bool forward = nextWp->bReverse ^ future_forward;
				F32 fDistFromLeg = aiCivCar_GetDesiredDistFromLeg(civ, wp, forward);
				const F32 *pvNextLegPos = aiCivilianLegGetPos(nextWp->leg, forward, 0);

				// using our current waypoint position, and a position in the next leg
				// calculate the intersection of the lanes to determine where we should move
				// our current waypoint to

				scaleAddVec3(nextWp->leg->perp, fDistFromLeg, pvNextLegPos, pt2);
				if (acgLineLine2dIntersection(wp->pos, wp->leg->dir, pt2, nextWp->leg->dir, vIsectPt))
				{
					copyVec3(vIsectPt, wp->pos);
				}
			}

			nextWp->bReverse = !calcLegFlow(wp->leg, nextWp->leg);
			aiCivilianMakeWpPos(&civ->civBase, nextWp->leg, nextWp, 1);
			aiCiv_AddNewWaypointToPath(&civ->civBase.path, nextWp);
		}
		else
		{
			// we are using an intersection
			devassert(nextWp->acpi);

			if(!nextWp->acpi->bIsMidIntersection)
			{
				AICivilianWaypoint *legWp, *stopWp = NULL;
				F32 fDot;

				if (nextWp->acpi->isectionType > EIntersectionType_NONE)
				{
					stopWp = civCarInsertStopWaypoint(civ, wp, nextWp, future_forward);
				}

				// end to end intersection,
				// this intersection may have several different legs we can go on.
				// (for now choose a random one)
				legWp = aiCivilianWaypointAlloc();
				legWp->bIsLeg = true;
				legWp->leg = aiCivilianACPIGetRandomLeg(nextWp->acpi, NULL);
				devassert(legWp->leg);

				// set the reverse flag as we go into the intersection
				nextWp->bReverse = !calcLegFlow(wp->leg, legWp->leg);
				aiCivilianMakeWpPos(&civ->civBase, legWp->leg, nextWp, 0);

				fDot = dotVec3(wp->leg->dir, legWp->leg->dir);

				// if the angles of the current leg and the legWp are not within
				// a threshold, we are making a turn.
				if (fabs(fDot) <= cosf(s_fCarTurnAngleThreshold))
				{
					// we need to insert a waypoint to round out the turn a bit,
					// so the car does not go over a sidewalk or encroach into another lane
					AICivilianPartitionState *pPartition = aiCivilian_GetPartitionState(CIV_BASE(civ).iPartitionIdx);
					AICivilianWaypoint *intermediateWp = aiCivilianWaypointAlloc();
					bool bFoundCurvePoint = false;

					devassert(pPartition);

					intermediateWp->bIsLeg = false;
					intermediateWp->acpi = nextWp->acpi;
					intermediateWp->lane = nextWp->lane;
					intermediateWp->medianDist = nextWp->medianDist;
					intermediateWp->bStop = false;

					intermediateWp->bReverse = nextWp->bReverse;
					nextWp->bReverse = false;
										
					if (pPartition->pathInfo.stashIntersectionCurves)
					{
						ACGCurveKey key = CreateKey(wp->leg, legWp->leg, wp->lane, nextWp->lane, 0);
						StashElement	e;

						if(stashFindElement(pPartition->pathInfo.stashIntersectionCurves, &key, &e))
						{
							AICivIntersectionCurve *pcurve = stashElementGetPointer(e);
							
							copyVec3(pcurve->vCurvePoint, intermediateWp->pos);
							bFoundCurvePoint = true;
						}
					}
					

					if (!bFoundCurvePoint)
					{
						acgLineLine2dIntersection(wp->pos, wp->leg->dir, nextWp->pos, 
													legWp->leg->dir, intermediateWp->pos);
					}

					if (stopWp)
					{
						stopWp->car_move_type = EAICivCarMove_GENERIC_TURN;
					}
					else
					{
						wp->car_move_type = EAICivCarMove_GENERIC_TURN;
					}

					aiCiv_AddNewWaypointToPath(&civ->civBase.path, intermediateWp);
				}
				//

				aiCiv_AddNewWaypointToPath(&civ->civBase.path, nextWp);
				//
				aiCivilianMakeWpPos(&civ->civBase, legWp->leg, legWp, 1);

				aiCiv_AddNewWaypointToPath(&civ->civBase.path, legWp);
			}
			else
			{
				// Midpoint intersections are special.
				PathLegIntersect *legPli = NULL;
				PathLegIntersect *intPt = NULL;
				//Vec3 intpos;

				{
					S32 i;

					// determine if we're coming from the intersection to the middle of the leg
					// or from the mid onto the next leg.
					for(i=0; i<eaSize(&nextWp->acpi->legIntersects); i++)
					{
						PathLegIntersect *pli = nextWp->acpi->legIntersects[i];

						if(!intPt && !vec3IsZero(pli->intersect))
							intPt = pli;

						if(pli->leg!=wp->leg)
							legPli = pli;
					}

					devassert(legPli && intPt);
				}

				if(legPli != intPt)
				{
					// coming from the 'side street' onto the middle of the intersection
					// on another leg

					AICivilianWaypoint *stopWp = NULL;
					F32 fDistFromLeg;

					// if this is a valid intersection type, insert a way
					if (nextWp->acpi->isectionType > EIntersectionType_NONE)
					{
						stopWp = civCarInsertStopWaypoint(civ, wp, nextWp, future_forward);
						stopWp->car_street_isect_type = EAICivCarStreetIsect_MID_SIDETOEDGE;
					}

					

					// add the intermediate waypoint that will help curve
					{
						Vec3 vLanePos;

						if (legPli->leg->median_width > 0.f)
						{
							// if we are going onto a road with a median, we can only make a right hand turn
							EAICivCarMove eCarMove = civCarClassifyTurn(wp->leg->dir, legPli->leg->dir, true);
							nextWp->bReverse = (eCarMove == EAICivCarMove_RIGHT_HAND_TURN) ? true : false;
						}
						else if (legPli->leg->bIsOneWay == false )
						{
							nextWp->bReverse = !!randomBool();  // Going from end to mid, can pick direction
						}
						else
						{
							nextWp->bReverse = (future_forward == 0);
						}

						if (stopWp)
						{	// if we have a stop waypoint, classify the turn type
							stopWp->car_move_type = civCarClassifyTurn(wp->leg->dir, legPli->leg->dir, nextWp->bReverse);
						}

						if(nextWp->bReverse)
						{
							future_forward = !future_forward;
						}

						if (!wp->leg->bSkewed_Start)
						{
							fDistFromLeg = aiCivCar_InitNextWayPtDistFromLeg(civ, legPli->leg, nextWp, future_forward);
							scaleAddVec3(legPli->leg->perp, fDistFromLeg, legPli->leg->start, vLanePos);
							acgLineLine2dIntersection(vLanePos, legPli->leg->dir, stopWp->pos, wp->leg->dir, nextWp->pos);
						}
						else 
						{
							int wpIsAtStart = distance3Squared(wp->leg->start, wp->pos) < distance3Squared(wp->leg->end, wp->pos);
							Vec3 vWpDir;

							if (wp->leg->bSkewed_Start)
							{
								Vec3 vDir;
								acgLeg_GetSkewedLaneDirection(wp->leg, vDir);
								crossVec3Up(vDir, vWpDir);
								normalVec3(vWpDir);
							}
							else
							{
								copyVec3(wp->leg->dir, vWpDir);
							}

							fDistFromLeg = aiCivCar_InitNextWayPtDistFromLeg(civ, legPli->leg, nextWp, future_forward);
							scaleAddVec3(legPli->leg->perp, fDistFromLeg, legPli->leg->start, vLanePos);
							acgLineLine2dIntersection(vLanePos, legPli->leg->dir, stopWp->pos, vWpDir, nextWp->pos);
							scaleAddVec3(legPli->leg->dir, 5.f, nextWp->pos, nextWp->pos);
						}
						

						aiCiv_AddNewWaypointToPath(&civ->civBase.path, nextWp);
					}

#if 0
					if (stopWp)
					{	// if we have a stop waypoint, classify the turn type
						stopWp->car_move_type = civCarClassifyTurn(wp->leg->dir, legPli->leg->dir, nextWp->bReverse);
					}
#endif 
					// create the last waypoint on the intersection's leg
					{
						#define SPLINE_MAX_DIST	50.0f

						AICivilianWaypoint *legWp = aiCivilianWaypointAlloc();
						legWp->bIsLeg = 1;
						legWp->leg = legPli->leg;
						aiCivilianMakeWpPos(&civ->civBase, legWp->leg, legWp, 1);

						if (distance3Squared(legWp->pos, nextWp->pos) > SQR(SPLINE_MAX_DIST))
						{
							// the next waypoint is too far away, we have to insert one before it
							AICivilianWaypoint *intermediateWp = aiCivilianWaypointAlloc();
							F32 fOffset;

							intermediateWp->bIsLeg = 1;
							intermediateWp->leg = legPli->leg;

							fOffset = (future_forward) ? SPLINE_MAX_DIST : -SPLINE_MAX_DIST;

							scaleAddVec3(legPli->leg->dir, fOffset, nextWp->pos, intermediateWp->pos);

							aiCiv_AddNewWaypointToPath(&civ->civBase.path, intermediateWp);
						}

						aiCiv_AddNewWaypointToPath(&civ->civBase.path, legWp);
					}
				}
				else
				{
					// Need to move wp point
					int flow = 0;
					//F32 fDistFromLeg;
					EAICivCarMove carMoveType = 0;

					stashAddressFindInt(legPli->leg->flowStash, wp->leg, &flow);

#ifdef CIVILIAN_PARANOID
					//devassert(flow!=ALF_MID);  // Basically, validate legPli==intPt
					if(flow==ALF_MID)
						printf("Found mid flow where it shouldn't be.");
#endif

					nextWp->bReverse = (flow != future_forward);
					if(nextWp->bReverse)
					{
						future_forward = !future_forward;
					}

					aiCivilianMakeWpPos(&civ->civBase, legPli->leg, nextWp, 0);
					
					carMoveType = civCarClassifyTurn(wp->leg->dir, legPli->leg->dir, nextWp->bReverse);

					if (legPli->leg->bSkewed_Start)
					{
						int wpIsAtStart = distance3Squared(legPli->leg->start, wp->pos) < distance3Squared(legPli->leg->end, wp->pos);
						Vec3 vWpDir;

						if (legPli->leg->bSkewed_Start)
						{
							Vec3 vDir;
							acgLeg_GetSkewedLaneDirection(legPli->leg, vDir);
							crossVec3Up(vDir, vWpDir);
						}
						else
						{
							copyVec3(legPli->leg->dir, vWpDir);
						}

						//fDistFromLeg = aiCivCar_InitNextWayPtDistFromLeg(civ, legPli->leg, nextWp, future_forward);
						//scaleAddVec3(legPli->leg->perp, fDistFromLeg, legPli->leg->start, vLanePos);
						acgLineLine2dIntersection(wp->pos, wp->leg->dir, nextWp->pos, vWpDir, wp->pos);

						//scaleAddVec3(legPli->leg->dir, 20.f, nextWp->pos, nextWp->pos);
					}
					else
					{
						acgLineLine2dIntersection(wp->pos, wp->leg->dir, nextWp->pos, legPli->leg->dir, wp->pos);
					}
					
					
					
					if (carMoveType == EAICivCarMove_LEFT_HAND_TURN)
					{
						AICivilianWaypoint *cornerWp;
						bool forward = (nextWp->bReverse) ? !future_forward : future_forward;

						{
							cornerWp = aiCivilianWaypointAlloc();
							memcpy(cornerWp, wp, sizeof(AICivilianWaypoint));
							cornerWp->bReverse = false;
							cornerWp->acpi = nextWp->acpi;
							cornerWp->bIsLeg = false;
						}

						{
							F32 stopDistance = forward ? -civ->width : civ->width;
							stopDistance *= 1.3f;
							scaleAddVec3(wp->leg->dir, stopDistance, wp->pos, wp->pos);
						}

						{
							AICivilianWaypoint *stopWp;
							stopWp = civCarInsertStopWaypoint(civ, wp, nextWp, forward);
							stopWp->car_street_isect_type = EAICivCarStreetIsect_MID_EDGETOSIDE;
							stopWp->car_move_type = EAICivCarMove_LEFT_HAND_TURN;
						}

						aiCiv_AddNewWaypointToPath(&civ->civBase.path, cornerWp);


					}
					else
					{
						// EAICivCarMove_RIGHT_HAND_TURN
						//

						// we will be blending out the turn, so insert a corner node for the spline
						AICivilianWaypoint *cornerWp;
						F32 stopDistance;
						bool forward = (nextWp->bReverse) ? !future_forward : future_forward;

						cornerWp = aiCivilianWaypointAlloc();
						memcpy(cornerWp, wp, sizeof(AICivilianWaypoint));
						cornerWp->bReverse = false;

						aiCiv_AddNewWaypointToPath(&civ->civBase.path, cornerWp);

						// move the current waypoint back a bit
						stopDistance = forward ? -civ->width : civ->width;
						stopDistance *= 1.3f;
						scaleAddVec3(wp->leg->dir, stopDistance, wp->pos, wp->pos);
						wp->car_move_type = EAICivCarMove_GENERIC_TURN;
					}
	

					aiCiv_AddNewWaypointToPath(&civ->civBase.path, nextWp);

					// create the last waypoint on the intersection's leg
					{
						AICivilianWaypoint *legWp = aiCivilianWaypointAlloc();
						legWp->bIsLeg = 1;
						legWp->leg = legPli->leg;
						aiCivilianMakeWpPos(&civ->civBase, legWp->leg, legWp, 1);
						aiCiv_AddNewWaypointToPath(&civ->civBase.path, legWp);
					}
				}


			}
		}
	}


	return true;
}




// ------------------------------------------------------------------------------------------------------------------
bool acgLineLine2dIntersection(const Vec3 l1_pt, const Vec3 l1_dir, const Vec3 l2_pt, const Vec3 l2_dir, Vec3 vIsectPos)
{
	F32 t = l2_dir[2] * l1_dir[0] - l2_dir[0] * l1_dir[2];
	if (t != 0)
	{
		t = (l2_dir[0] * (l1_pt[2] - l2_pt[2]) - l2_dir[2] * (l1_pt[0] - l2_pt[0])) / t;

		vIsectPos[0] = l1_pt[0] + l1_dir[0] * t;
		vIsectPos[1] = l1_pt[1];
		vIsectPos[2] = l1_pt[2] + l1_dir[2] * t;
		return true;
	}

	// lines are parallel
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
bool lineSegLineSeg2dIntersection(const Vec3 l1_st, const Vec3 l1_end, const Vec3 l2_st, const Vec3 l2_end, Vec3 vIsectPos)
{
	Vec3 vDir1, vDir2;

	subVec3(l1_end, l1_st, vDir1);
	subVec3(l2_end, l2_st, vDir2);

	if (acgLineLine2dIntersection(l1_st, vDir1, l2_st, vDir2, vIsectPos))
	{
		subVec3(vIsectPos, l1_st, vDir1);
		subVec3(vIsectPos, l1_end, vDir2);

		if (dotVec3(vDir1, vDir2) <= 0.0f)
		{
			// intersection point is within the first line
			subVec3(vIsectPos, l2_st, vDir1);
			subVec3(vIsectPos, l2_end, vDir2);
			if (dotVec3(vDir1, vDir2) <= 0.0f)
			{
				// intersection within the second line
				return true;
			}
		}
	}

	return false;
}
