#include "aiCivilianTrolley.h"
#include "AICivMovement.h"

#include "entity.h"
#include "gslMapState.h"
#include "timing.h"

static const F32 s_fTrolley_UpdatePerSeconds = 0.1f;

static AICivilianFunctionSet	s_trolleyFunctionTable;
F32 s_fCivTrolleyStopDistance = 35.f;

AICivilianFunctionSet* aiCivTrolley_GetFunctionSet()
{
	return &s_trolleyFunctionTable;
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivTrolley_FixupTrolleyDef(AICivilianDef *pDef)
{

}

// ------------------------------------------------------------------------------------------------------------------
int aiCivTrolley_ValidateTrolleyDef(AICivilianDef *pDef, const char *pszFilename)
{
	return true;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivTrolley_FixupTypeDef(AICivVehicleTypeDef *pDef)
{

}

// ------------------------------------------------------------------------------------------------------------------
int aiCivTrolley_ValidateTypeDef(AICivVehicleTypeDef *pDef, const char *pszFilename)
{
	return true;
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivTrolley_Initialize(Entity *e, AICivilianTrolley *civ, const AICivilianSpawningData *spawnData) 
{
	Vec3 vBoundsMin, vBoundsMax;

	entGetLocalBoundingBox(e, vBoundsMin, vBoundsMax, false);
	//civ->width = aiCivState.map_def->carLaneWidth;
	//civ->depth = vBoundsMax[0] - vBoundsMin[0];
	civ->length = vBoundsMax[0] - vBoundsMin[0];
	
	aiCivBlock_Initialize(&civ->blockInfo);

	mmAICivilianMovementSetDesiredSpeed(CIV_BASE(civ).requester, 
										CIV_BASE(civ).civDef->fSpeedMin, 
										CIV_BASE(civ).civDef->fSpeedRange);
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivTrolley_Free(AICivilianTrolley *civ)
{
	
}


// ------------------------------------------------------------------------------------------------------------------
void aiCivTrolley_Tick(Entity *e, AICivilianTrolley *civ)
{
	Vec3 vFacingDir;
	Vec3 vPos;
	int partitionIdx = entGetPartitionIdx(e);

	PERFINFO_AUTO_START("trolley", 1);

	if(!entViewIsCurrent(e, pyFaceViewIsAtRest))
	{
		civ->civBase.v2DirIsCurrent = 0;
	}

	if(ABS_TIME_SINCE_PARTITION(partitionIdx, civ->civBase.lastUpdateTime) < SEC_TO_ABS_TIME(s_fTrolley_UpdatePerSeconds))
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

	

	if(FALSE_THEN_SET(civ->civBase.v2DirIsCurrent))
	{

/*
		{
			Vec2 v2PitchYaw;
			entGetFacePY(e, v2PitchYaw);
			sincosf(v2PitchYaw[1], &vFacingDir[0], &vFacingDir[2]);
			vFacingDir[1] = 0.0f;
		}
*/
		aiCiv_GetCurrentWayPointDirection(e, &civ->civBase, vFacingDir);
		civCopyVec3ToVec2(vFacingDir, civ->civBase.v2Dir);
		// civCopyVec3ToVec2(vFacingDir, civ->civBase.v2Dir);

	}

	civ->civBase.lastUpdateTime = ABS_TIME_PARTITION(partitionIdx);
	aiCivilianProcessPath(e, &civ->civBase, vPos, vFacingDir);

	PERFINFO_AUTO_STOP();
}


// ------------------------------------------------------------------------------------------------------------------
F32 aiCivTrolley_GetDesiredDistFromLeg(const AICivilianTrolley *civ, const AICivilianWaypoint *relativeWaypt, int forward)
{
	return 0.f;
}

// ------------------------------------------------------------------------------------------------------------------
F32 aiCivTrolley_InitNextWayPtDistFromLeg(const AICivilianTrolley *civ,  const AICivilianPathLeg *leg, AICivilianWaypoint *pNewWaypoint, int forward)
{
	return 0.f;
}

// ------------------------------------------------------------------------------------------------------------------
bool aiCivTrolley_ContinuePath(AICivilianTrolley *civ)
{
	AICivilianWaypoint *pCurrentWp = aiCivilianGetFromLastWaypoint(&civ->civBase, 0);
	const AICivilianPathPoint *pNextPathPoint = NULL;
	
	if (!pCurrentWp)
		return false;

	if (!pCurrentWp->pPathPoint->pNextPathPoint)
	{
		AICivilianPathPointIntersection *pPPIsect;
		S32 idx;
		if (!pCurrentWp->pPathPoint->pPathPointIntersection)
			return false; // dead end path
		
		pPPIsect = pCurrentWp->pPathPoint->pPathPointIntersection;

		idx = pPPIsect->iUseIdx;
		pPPIsect = pCurrentWp->pPathPoint->pPathPointIntersection;
		FOR_EACH_IN_EARRAY(pPPIsect->eaPathPoints, AICivilianPathPoint, pPathPoint)
		{
			if (pPathPoint == pCurrentWp->pPathPoint)
				continue;

			pNextPathPoint = pPathPoint;
			if (!idx)
				break;
			idx--;
		}
		FOR_EACH_END

		pPPIsect->iUseIdx++;
		if (pPPIsect->iUseIdx >= eaSize(&pPPIsect->eaPathPoints) -1)
		{
			pPPIsect->iUseIdx = 0;
		}

		if (!pNextPathPoint)
			return false;
		
	}
	else
	{
		pNextPathPoint = pCurrentWp->pPathPoint->pNextPathPoint;
	}
	


	/*
	if (pCurrentWp->bWasStop)
	{
		pNextPathPoint = pCurrentWp->pPathPoint;
	}
	else
	{
		
	}
	*/

	{
		AICivilianWaypoint *pNextWp = aiCivilianWaypointAlloc();
		
		pNextWp->pPathPoint = pNextPathPoint;
		copyVec3(pNextPathPoint->vPos, pNextWp->pos);

		// if we have an intersection coming up , we need to add a stop waypoint
		if(pNextPathPoint->pIntersection)
		{
			// set up the next wp to be a stop
			pNextWp->pPathPointIsect = pNextPathPoint->pIntersection;
			pNextWp->bStop = pNextWp->bWasStop = true;
		}
		else if (pCurrentWp->pPathPoint->bIsReversalPoint)
		{
			pNextWp->bIsUTurn = true;
			pNextWp->bStop = pNextWp->bWasStop = true;
		}
		

		
		
		aiCiv_AddNewWaypointToPath(&civ->civBase.path, pNextWp);
		return true;
	}
	
	return false;
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivTrolley_ReachedWaypoint(AICivilianTrolley *civ, AICivilianWaypoint *curWp)
{
	
}

// ------------------------------------------------------------------------------------------------------------------
void aiCivTrolley_ProcessPath(Entity *e, AICivilianTrolley *civ, const Vec3 vPos, const Vec3 vFacingDir)
{
	AICivilianWaypoint *curWp = NULL;
	
	if (civ->blockInfo.eBlockType > EBlockType_NONE)
	{
		bool bIsBlocked;
		EBlockType blockType;
		blockType = aiCivBlock_UpdateBlocking(&civ->civBase, &civ->blockInfo);
		bIsBlocked = (EBlockType_NONE != blockType);
		if(bIsBlocked)
		{
			mmAICivilianMovementSetDesiredSpeed(civ->civBase.requester, 0.0f, 0.0f);
			return;
		}
	}

	mmAICivilianMovementSetDesiredSpeed(CIV_BASE(civ).requester, 
										CIV_BASE(civ).civDef->fSpeedMin, 
										CIV_BASE(civ).civDef->fSpeedRange);

	curWp = aiCivilianGetCurrentWaypoint(&civ->civBase);
	if (curWp && curWp->bStop)
	{
		if (curWp->pPathPointIsect)
		{
			if (aiCivIntersection_CanTrolleyCross(curWp->pPathPointIsect, civ))
			{
				curWp->bStop = false;
			}
		}
		else if (curWp->bIsUTurn)
		{
			if (civ->stopTime == 0)
			{
				civ->stopTime = timeSecondsSince2000() + 1.75f;
			}
			else if (timeSecondsSince2000() > civ->stopTime)
			{
				curWp->bStop = false;
				civ->stopTime = 0;
			}
		}
	}

	aiCivilianCheckReachedWpAndContinue(e, &civ->civBase, vPos);

}


void aiCivTrolley_InitializeData()
{
	s_trolleyFunctionTable.CivInitialize = (fpCivInitialize) aiCivTrolley_Initialize;
	s_trolleyFunctionTable.CivFree = (fpCivFree) aiCivTrolley_Free;
	s_trolleyFunctionTable.CivGetDesiredDistFromLeg = (fpCivGetDesiredDistFromLeg) aiCivTrolley_GetDesiredDistFromLeg;
	s_trolleyFunctionTable.CivInitNextWayPtDistFromLeg = (fpCivInitNextWayPtDistFromLeg) aiCivTrolley_InitNextWayPtDistFromLeg;
	s_trolleyFunctionTable.CivContinuePath = (fpCivContinuePath) aiCivTrolley_ContinuePath;
	s_trolleyFunctionTable.CivReachedWaypoint = (fpCivReachedWaypoint) aiCivTrolley_ReachedWaypoint;
	s_trolleyFunctionTable.CivProcessPath = (fpCivProcessPath) aiCivTrolley_ProcessPath;
}
