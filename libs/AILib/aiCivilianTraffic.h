#pragma once

#include "aiCivilianPrivate.h"

// ---------------------------------------------------------------
// Ai Traffic

typedef struct AICivilianTrafficQuery AICivilianTrafficQuery;
typedef struct AICivilian AICivilian;
typedef struct AICivilianPathLeg AICivilianPathLeg;
typedef struct AICivStopSignUser AICivStopSignUser;
typedef struct AICivilianPathIntersection AICivilianPathIntersection;
typedef struct AICivilianCar AICivilianCar;
typedef struct AICivilianTrolley AICivilianTrolley;
typedef struct AICivilianPartitionState AICivilianPartitionState;

typedef struct AICivilianBlockInfo
{
	EntityRef			entRefBlocker;
	F32					fBlockDistSq;
	AICivilianPathLeg*	pBlockingCrosswalk;
	EBlockType			eBlockType;
} AICivilianBlockInfo;

void aiCivBlock_Initialize(AICivilianBlockInfo *pBlocker);
EBlockType aiCivBlock_UpdateBlocking(AICivilian *pBlockee, AICivilianBlockInfo *pBlockInfo);

// ---------------------------------------------------------------
void aiCivTraffic_InitStatic();
void aiCivTraffic_Create(AICivilianPartitionState *pPartition);
void aiCivTraffic_Destroy(AICivilianPartitionState *pPartition);
void aiCivTraffic_OncePerFrame(AICivilianPartitionState *pPartition);

// ---------------------------------------------------------------
// Traffic Queries
void aiCivCrossTrafficManagerReleaseQuery(AICivilianTrafficQuery **query);
AICivilianTrafficQuery* aiCivXTrafficCreateQuery(S32 iPartitionIdx, AICivilian *civ, const Vec3 srcPs, const Vec3 dir, F32 len, F32 half_width,
												 const AICivilianPathLeg *leg, SA_PARAM_OP_VALID AICivilianTrafficQuery *head_query);

bool aiCivXTrafficQueryIsSatisfied(AICivilianTrafficQuery *query);
bool aiCivStopLight_IsWayGreen(const AICivilianPathIntersection *acpi, const AICivilianPathLeg *leg);

bool aiCivIntersection_CanTrolleyCross(AICivilianPathIntersection *acpi, AICivilianTrolley *pTrolley);


// ---------------------------------------------------------------
// Stop Sign
void aiCivStopSignUserFree(AICivStopSignUser **pacssu);
bool aiCivStopSignUserCheckACPI(const AICivStopSignUser *pacssu, const AICivilianPathIntersection *acpi);
void aiCivStopSignUserAdd(AICivilianPathIntersection *acpi, AICivilianCar *civ, const Vec3 vStart, const Vec3 vEnd, const AICivilianPathLeg *destLeg, const AICivilianPathLeg *srcLeg);
bool aiCivStopSignIsWayClear(const AICivilianPathIntersection *acpi, const AICivilian *civ, const Vec3 vStart, const Vec3 vEnd, const AICivilianPathLeg *destLeg, const AICivilianPathLeg *srcLeg);

// ---------------------------------------------------------------
// Blocking
void aiCivCarBlockManager_ReportAddedLeg(AICivCarBlockManager *pCarBlockManager, AICivilianPathLeg *leg);
void aiCivCarBlockManager_ReportRemovedLeg(AICivilianPathLeg *leg);

void aiCivCarBlockManager_ReportAddedIntersection(AICivCarBlockManager *pCarBlockManager, AICivilianPathIntersection *acpi);
void aiCivCarBlockManager_ReportRemovedIntersection(AICivilianPathIntersection *acpi);



//void aiCivCarBlockManager_RemoveLeg(AICivilianPathLeg *leg);
//void aiCivCarBlockManager_RemoveIntersection(AICivilianPathIntersection *acpi);

// ---------------------------------------------------------------
// Crosswalks
bool aiCivCrosswalk_CanCivCross(const AICivilianPathLeg *pLeg);

bool aiCivCrosswalk_CanCivUseCrosswalk(const AICivilianPathLeg *pLeg);

AICivCrosswalkUser* aiCivCrosswalk_CreateAddUser(AICivilianPathLeg *pLeg, AICivilian *pCiv, bool bStart);
void aiCivCrosswalk_SetWaypointWaitingPos(const AICivilianWaypoint *pXWalkWp, AICivilianWaypoint *pWaitingWp);
void aiCivCrosswalk_SetUserWaiting(AICivCrosswalkUser *pUser);
void aiCivCrosswalk_SetUserCrossingStreet(AICivCrosswalkUser *pUser);
bool aiCivCrosswalk_IsCrosswalkOpen(AICivCrosswalkUser *pUser);

void aiCivCrosswalk_ReleaseUser(AICivCrosswalkUser **ppUser);

