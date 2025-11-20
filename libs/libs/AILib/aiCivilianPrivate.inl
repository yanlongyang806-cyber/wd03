#include "MemoryPool.h"
#include "Entity.h"
extern MP_DEFINE_MEMBER(AICivilianWaypoint);

// ------------------------------------------------------------------------------------------------------------------
__forceinline static AICivilianWaypoint* aiCivilianWaypointAlloc(void)
{
	return MP_ALLOC(AICivilianWaypoint);
}

__forceinline static AICivilianWaypoint* aiCivilianGetCurrentWaypoint(AICivilian *civ)
{
	if (civ->path.curWp < eaSize(&civ->path.eaWaypoints))
	{
		ANALYSIS_ASSUME(civ->path.eaWaypoints);
		return civ->path.eaWaypoints[civ->path.curWp];
	}
	return NULL;
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static AICivilianWaypoint* aiCivilianGetFromLastWaypoint(const AICivilian *civ, S32 iNumWayPtsBack)
{
	S32 idx = (eaSize(&civ->path.eaWaypoints) - 1) - iNumWayPtsBack;

	return eaGet(&civ->path.eaWaypoints, idx);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static AICivilianWaypoint* aiCiv_LastLegWaypoint(const AICivilian *civ)
{
	S32 idx = eaSize(&civ->path.eaWaypoints);

	while(--idx >= 0)
	{
		AICivilianWaypoint *wp = civ->path.eaWaypoints[idx];
		if (wp->bIsLeg)
			return wp;
	}

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
// Returns what the forward flag will be once the civilian gets to the last waypoint
__forceinline static int aiCivilianCalculateFutureForward(const AICivilian *civ)
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
	}

	return forward;
}

// ------------------------------------------------------------------------------------------------------------------
// Returns true if the legs continue in the same direction
//	false if they are in opposition
__forceinline int calcLegFlow(const AICivilianPathLeg *leg, const AICivilianPathLeg *other_leg)
{
	int legFlow, otherFlow;

	stashAddressFindInt(leg->flowStash, other_leg, &legFlow);
	stashAddressFindInt(other_leg->flowStash, leg, &otherFlow);

#ifdef CIVILIAN_PARANOID
	devassertmsg(legFlow!=2 && otherFlow!=2, "Using leg flow where not meaningful");
#endif
	return legFlow!=otherFlow;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void aiCiv_AddNewWaypointToPath(AICivilianPath *path, AICivilianWaypoint *wp)
{
	eaPush(&path->eaAddedWaypoints, wp);
	eaPush(&path->eaWaypoints, wp);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static const F32* aiCivilianLegGetPos(const AICivilianPathLeg *leg, int forward, int end)
{
	return (forward ^ end) ? leg->start : leg->end ;
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static AICivilianWaypoint* aiCivilianGetNextWaypoint(AICivilian *civ)
{
	ANALYSIS_ASSUME(civ->path.eaWaypoints);
	if (civ->path.curWp + 1 < eaSize(&civ->path.eaWaypoints))
	{	
		//devassert(civ->path.curWp + 1 < eaSize(&civ->path.eaWaypoints));
		return civ->path.eaWaypoints[civ->path.curWp + 1];
	}
	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static AICivilianWaypoint* aiCivilianGetNextNextWaypoint(AICivilian *civ)
{
	ANALYSIS_ASSUME(civ->path.eaWaypoints);
	devassert(civ->path.curWp + 2 < eaSize(&civ->path.eaWaypoints));
	return civ->path.eaWaypoints[civ->path.curWp + 2];
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static AICivilianWaypoint* aiCivilianGetNextLegWaypoint(AICivilian *civ)
{
	S32 i, size;

	ANALYSIS_ASSUME(civ->path.eaWaypoints);
	size = eaSize(&civ->path.eaWaypoints);
	i = civ->path.curWp + 1;
	do {
		AICivilianWaypoint *wp = civ->path.eaWaypoints[i];
		if (wp->bIsLeg)
		{
			return wp;
		}

	} while (++i < size);

	return NULL;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static F32 acgLeg_GetSkewedLaneLength(const AICivilianPathLeg *leg)
{
	return leg->fSkewedLength_Start;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void acgLeg_GetSkewedLaneDirection(const AICivilianPathLeg *leg, Vec3 vDir)
{
	setVec3(vDir, sinf(leg->fSkewedAngle_Start), 0, cosf(leg->fSkewedAngle_Start));
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline void aiCivIntersection_GetBoundingMidPos(const AICivilianPathIntersection *acpi, Vec3 vMid)
{
	interpVec3(0.5f, acpi->min, acpi->max, vMid);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline int aiCivilian_IsDoingCivProcessingOnly(Entity *e, AICivilian *civ)
{
	return entCheckFlag(e, ENTITYFLAG_CIV_PROCESSING_ONLY);
}