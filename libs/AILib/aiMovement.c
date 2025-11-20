#include "aiMovement.h"

#include "aiAvoid.h"
#include "aiConfig.h"
#include "aiDebug.h"
#include "aiDebugShared.h"
#include "aiFormation.h"
#include "aiLib.h"
#include "aiPowers.h"
#include "aiTeam.h"
#include "aiAmbient.h"

#include "CombatConfig.h"
#include "EntityMovementDoor.h"
#include "EntityMovementFX.h"
#include "Powers.h"
#include "BeaconPath.h"
#include "EntityMovementDefault.h"
#include "EntityMovementFlight.h"
#include "EntityMovementManager.h"
#include "GlobalTypes.h"
#include "gslCritter.h"
#include "gslEncounter.h"
#include "gslInteractable.h"
#include "gslMapState.h"
#include "gslOldEncounter.h"
#include "gslPatrolRoute.h"
#include "LineDist.h"
#include "MemoryPool.h"
#include "Player.h"
#include "StringCache.h"
#include "StateMachine.h"
#include "entCritter.h"
#include "WorldGrid.h"
#include "WorldLib.h"
#include "qsortG.h"
#include "rand.h"

typedef struct NavPathWaypoint NavPathWaypoint;

#include "AILib_autogen_QueuedFuncs.h"
#include "aiMovement_h_ast.h"
#include "aiMovement_c_ast.h"
#include "beaconPath_h_ast.h"

#define MAX_FLIGHT_PITCH (HALFPI - 0.01)
#define CLOSE_ENOUGH_DIST 0.5f

static int globalDisableShortcutting;
AUTO_CMD_INT(globalDisableShortcutting, disableShortcutting);

static int globalAIDisableAirMove = 0;
AUTO_CMD_INT(globalAIDisableAirMove, globalAIDisableAirMove);

static F32 globalPseudoSplineTest = 0;
AUTO_CMD_FLOAT(globalPseudoSplineTest, aiSplineMoveTest);

static int globalWPParanoia = 1;
AUTO_CMD_INT(globalWPParanoia, globalWPParanoia);

static int g_ForceFailPathfindDist = 0;
AUTO_CMD_INT(g_ForceFailPathfindDist, ForceFailPathfindDist);

static int aiMovementMakePath(	Entity* e, AIVarsBase* aib, NavPath *navPath, const Vec3 source, 
								const Vec3 target, int* canShortcut, int *didPathfindOut, 
								NavSearchResultType *resultOut,
								U32 inAvoid, AIMovementTargetFlags flags);

//#define AI_MOVEMENTTARGET_PARANOID

AUTO_RUN_MM_REGISTER_UNSYNCED_REQUESTER_MSG_HANDLER(aiMovementMsgHandler,
													"AIMovement",
													AIMovement);

// gets the standard needed aiCollideRay flags. 
#define STD_AICOLLIDERAY_FLAGS(isFlying, inAvoid) (((!inAvoid) ? AICollideRayFlag_DOAVOIDCHECK : 0)|\
												  ((!isFlying)?AICollideRayFlag_DOWALKCHECK:AICollideRayFlag_DOCAPSULECHECK))

#define aiMovementTagFGForPathUpdate(e, aib, fg) aiMovementTagFGForPathUpdateEx(e, aib, fg, __FILE__, __LINE__)
#define aiMovementUpdateBGNavSearch(e, aib) aiMovementUpdateBGNavSearchEx(e, aib, __FILE__, __LINE__)


AUTO_STRUCT;
typedef struct AIMovementAnimFXHandle {
	const char*					name;	AST(POOL_STRING)
	U32							bitHandle;
	S64							timeStamp;
	U32							mmfxHandle;
	U32							aiMovementHandle;
	U8							destroyAfterOneTick : 1;
} AIMovementAnimFXHandle;

MP_DEFINE(AIMovementAnimFXHandle);
AIMovementAnimFXHandle* aiMovementAnimFXHandleCreate(void)
{
	MP_CREATE(AIMovementAnimFXHandle, 16);
	return MP_ALLOC(AIMovementAnimFXHandle);
}

void aiMovementAnimFXHandleDestroy(AIMovementAnimFXHandle* handle)
{
	MP_FREE(AIMovementAnimFXHandle, handle);
}

AUTO_STRUCT;
typedef struct AIMovementToFGRefine
{
	NavPathWaypoint*			refineInsertWp; AST(UNOWNED)
	Vec3						refineStartPos;
	Vec3						refineTarget;
	U32							useRefineStartPos;
	const char*					caller_fname; AST(UNOWNED)
	int							line;
	U32							useRefineTargetPos : 1;  // For avoid only
	U32							refineInsertReverse : 1;
	U32							pathResetOnRefine : 1;
	U32							requestRefine : 1;
	U32							refineAgain : 1;
	U32							outOfAvoidArea : 1;
}AIMovementToFGRefine;

AUTO_STRUCT;
typedef struct AIMovementQueuedDoor {
	Vec3						pos;
} AIMovementQueuedDoor;

AUTO_STRUCT;
typedef struct AIMovementUpdateToFGUpdated {
	U32 movementCompleted : 1;
} AIMovementUpdateToFGUpdated;

AUTO_STRUCT;
typedef struct AIMovementUpdateToFG {
	U32 movementCompleted : 1;

	AIMovementUpdateToFGUpdated updated;
} AIMovementUpdateToFG;

AUTO_STRUCT;
typedef struct AIMovementToFG {
	Vec3						splineTarget;	// Send to FG for debug
	F32							distMovedSinceSync;
	NavPathWaypoint**			wpPassed; AST(UNOWNED)
	NavPathWaypoint**			wpBadConn; AST(UNOWNED)
	NavPathWaypoint**			wpBadConnReset; AST(UNOWNED)
	NavPathWaypoint**			wpDestroy; AST(UNOWNED)
	AIVolumeEntry**				avoidDestroy; NO_AST
	NavPathWaypoint**			wpDebugCurPath; AST(UNOWNED)
	int							wpDebugCurWp;
	AIDebugLogEntry**			logEntries;
	AIMovementAnimFXHandle**	animFxDestroy; AST(UNOWNED)
	AIMovementToFGRefine		refine;
	AIMovementQueuedDoor*		queuedDoor;
	U32							id;
	AIMovementUpdateToFG		updateToFG;
	U32							enabled : 1;
	U32							deleteOldPath : 1;
	U32							turnOnFlight : 1;
	U32							turnOffFlight : 1;
} AIMovementToFG;

AUTO_STRUCT;
typedef struct AIMovementUpdate {
	U8							sleeping : 1;
	U8							path : 1;
	U8							orders : 1;
	U8							metaorders : 1;
	U8							animBits : 1;
	U8							animHold : 1;
	U8							followTargetPos : 1;
	U8							config : 1;
	U8							rotation : 1;
	U8							refine : 1;
	U8							avoid : 1;
	U8							doorComplete : 1;
	U8							flying : 1;
	U8							debugForceProcess : 1;
	U8							teleportDisabled : 1;
	U8							rotationDisabled : 1;
} AIMovementUpdate;

AUTO_ENUM;
typedef enum AIMovementRotationType
{
	AI_MOVEMENT_ROTATION_NONE = 0,
	AI_MOVEMENT_ROTATION_POS,
	AI_MOVEMENT_ROTATION_ROT,
	AI_MOVEMENT_ROTATION_ENTREF,
}AIMovementRotationType;

AUTO_STRUCT;
typedef struct AIMovementRotationInfo
{
	AIMovementRotationType		type;
	union {
		Vec3					finalFacePos; AST(REDUNDANTNAME)
		EntityRef				finalFaceEntRef; AST(REDUNDANTNAME)
		Quat					finalFaceRot;
	};	
}AIMovementRotationInfo;

AUTO_STRUCT;
typedef struct AIMovementOrders
{
	AIMovementOrderType			movementType;
	AIMovementOrderEntDetail	entDetail;

	// Target ref specifies the entity you're following/patrolling with
	EntityRef					targetRef;
	Vec3						targetPos;
	
	// Target offset specifies where, relative to the targetRef, you're going
	Vec3						targetOffset;
	U32							useOffset : 1;
	U32							pathfindResult : 1;
	U32							offsetRotRelative : 1;
	U32							stopWithinRange : 1;

	// If the order is settled, you follow the settled pos
	U32							settled : 1;
	Vec3						settledPos;
}AIMovementOrders;

AUTO_STRUCT;
typedef struct AIMovementMetaOrders
{
	// These two distances specify how far back the critter will let himself be before running
	//   and how close he gets before he starts walking 
	F32							distWalk;
	F32							distRun;

	// If speedCheat is true, the critter will smartly override his max speed to catch up
	U32							speedCheat : 1;
} AIMovementMetaOrders;

AUTO_STRUCT;
typedef struct AIMovementFG {
	AIMovementUpdate			updated;

	AIMovementConfigSettings	config;
	AIMovementRotationInfo		rotation;

	AIMovementOrders			orders;
	AIMovementMetaOrders		metaorders;

	Vec3						splineTarget;	// Send to FG for debug
	Vec3						lastWPPos;
	U32*						animHold; AST(UNOWNED)
	AIMovementAnimFXHandle**	animFxAdd; AST(UNOWNED)
	AIMovementAnimFXHandle**	animFxCancel; AST(UNOWNED)
	NavPath						pathNormal;
	NavPath						pathRefine;
	NavPathWaypoint*			refineInsertWp; AST(UNOWNED)
	AIVolumeEntry**				avoidAdd; NO_AST
	AIVolumeEntry**				avoidRemove; NO_AST
	AIMovementTargetFlags		flags; AST(INT)
	U32							refineId;
	U32							id;
	U32							doMove : 1;
	U32							pathReset : 1;
	U32							needsTeleport : 1;
	U32							sleeping : 1;
	U32							refineInsertReverse : 1;
	U32							refineAgain : 1;
	U32							flying : 1;
	U32							debugForceProcess : 1;
	U32							teleportDisabled : 1;
	U32							rotationDisabled : 1;

	NavPathWaypoint**			wpPassed; AST(UNOWNED)
	NavPathWaypoint**			wpDestroy; AST(UNOWNED)

#ifdef AI_PARANOID_AVOID
	U32							timeStamp; NO_AST
	AIVolumeEntry**				adds; NO_AST
	AIVolumeEntry**				removes; NO_AST
#endif
} AIMovementFG;

AUTO_STRUCT;
typedef struct AIMovementToBG {
	AIMovementUpdate			updated;

	AIMovementConfigSettings	config;
	AIMovementRotationInfo		rotation;

	AIMovementOrders			orders;
	AIMovementMetaOrders		metaorders;

	U32*						animHold; AST(UNOWNED)
	AIMovementAnimFXHandle**	animFxAdd; AST(UNOWNED)
	AIMovementAnimFXHandle**	animFxCancel; AST(UNOWNED)
	NavPathWaypoint**			waypointsPath; AST(UNOWNED)
	U32							curWaypoint;
	NavPathWaypoint**			waypointsRefine; AST(UNOWNED)
	NavPathWaypoint*			refineInsertWp; AST(UNOWNED)
	AIVolumeEntry**				avoidAdd; NO_AST
	AIVolumeEntry**				avoidRemove; NO_AST
	AIMovementTargetFlags		flags; AST(INT)

	U32							id;
	U32							circular : 1;
	U32							pingpong : 1;
	U32							pingpongRev : 1;
	U32							pathReset : 1;
	U32							doMove : 1;
	U32							sleeping : 1;
	U32							refineInsertReverse : 1;
	U32							refineAgain : 1;
	U32							flying : 1;
	U32							canFly : 1;
	U32							alwaysFlying : 1;		// Flight from innate or class, not mutable
	U32							debugForceProcess : 1;
	U32							teleportDisabled : 1;
	U32							rotationDisabled : 1;

#ifdef AI_PARANOID_AVOID
	U32							timeStampFromFG;  NO_AST
#endif
} AIMovementToBG;

AUTO_ENUM;
typedef enum AIMovementReferencePosType {
	AIM_RPT_WAYPOINT,
	AIM_RPT_OTHER,
} AIMovementReferencePosType;

typedef struct AIMovementBG AIMovementBG;
typedef void (*AIMovementRotationFunc)(const MovementRequesterMsg *msg, AIMovementBG *bg, Quat target);
typedef struct AIMovementRotationHandler
{
	Quat faceOut;

	AIMovementRotationFunc func;
} AIMovementRotationHandler;

typedef struct AIMovementRotationHandlerDefault
{
	AIMovementRotationHandler _base;
} AIMovementRotationHandlerDefault;

typedef struct AIMovementRotationHandlerSmooth
{
	AIMovementRotationHandler _base;

	Quat steadyStateFacing;
	U32 lastSteadyState;
} AIMovementRotationHandlerSmooth;

AUTO_STRUCT;
typedef struct AIMovementBG {
	AIMovementConfigSettings	config;
	AIMovementRotationInfo		rotation;
	AIMovementToFGRefine		refine;
	AIMovementOrders			orders;
	AIMovementMetaOrders		metaorders;

	Vec3						targetVelAvg;	// Used to "predict" target's movement
	Vec3						lastTargetEntPos;
	Vec3						targetEntPos;

	Vec3						targetPos;
	Vec3						splineTarget;	// Used for splining around past waypoints
	Vec3						splineSource;
	Vec3						lastPos;
	Vec3						dir;
	Vec3						targetPosLastFrame;
	Quat						faceRot;
	Vec3						jumpPos;
	U32*						animHold; AST(UNOWNED)
	AIMovementAnimFXHandle**	animFxList; AST(UNOWNED)
	AIVolumeEntry**				avoidEntries; NO_AST
	NavPath						path; NO_AST
	AIMovementTargetFlags		flags; AST(INT)
	F32							recentMinDistToTarget;
	F32							recentMaxVelDotToTarget;
	F32							overrideSpeed;
	U32							id;
	S32							stuckCounter;
	U32							refineRequestWait;
	S64							timeLastShortcutCheck;
	S64							timeLastRefineCheck;
	S64							timeLastJump;
	S64							timeLastSpline;
	S64							timeLastGroundCheck;
	S64							timeLastReachableCheck;
	S64							timeLastTargetDistCheck;
	S64							timeLastProcess;
	U32							spcTimeLastCheckedCombatOffset;
	U32							spcTimeInRangeOfCombatOffset;
	Vec3						posLastTargetDistCheck;
	AIMovementReferencePosType  refPosType;
	AIMovementUpdateToFG		updateToFG;

	AIMovementRotationHandler	*rotationHandler;		NO_AST

	S8							combatOffsetCheckCounter;
	U32							doMove						: 1;
	U32							doMoveDone					: 1;
	U32							refineRequested				: 1;
	U32							runningDoor					: 1;
	U32							sleeping					: 1;
	U32							doTeleport					: 1;
	U32							skipStuckDetection			: 1;
	U32							flying						: 1;		// Set by powers code
	U32							canFly						: 1;		// Has a flight power at all
	U32							shouldFly					: 1;  // Wait for flying to equal this
	U32							flyingOnGround				: 1;
	U32							alwaysFlying				: 1;	// Flight from innate or class, not mutable
	U32							splining					: 1;
	U32							targetUnreachable			: 1;
	U32							debugForceProcess			: 1;
	U32							approachingNext				: 1;
	U32							walking						: 1;
	U32							running						: 1;
	U32							usePosLastTargetDistCheck	: 1;
	U32							teleportDisabled			: 1;
	U32							reachedOffset				: 1;
	U32							triedMovingLastFrame		: 1;
	U32							animHoldClear				: 1;
	U32							rotationDisabled			: 1;

#ifdef AI_PARANOID_AVOID
	U32							lastTimeStampFromFG;  NO_AST
	AIVolumeEntry**				adds;		NO_AST
	AIVolumeEntry**				removes;	NO_AST
#endif
} AIMovementBG;

AUTO_STRUCT;
typedef struct AIMovementLocalBG {
	S32					unused;
} AIMovementLocalBG;

AUTO_STRUCT;
typedef struct AIMovementSync {
	U32					unused;
} AIMovementSync;

U32 aiMovementGetNewId()
{
	static int id = 0;
	return ++id;
}

static void aiMovementSetMovementType(AIMovementOrders *orders, AIMovementOrderType type, int detail)
{
	orders->movementType = type;
	orders->entDetail = detail;
}

// ------------------------------------------------------------------------------------------------------------
// returns true if the next waypoint is the end of the current path
// reversing directions on a pingpong path and wrapping in a circular path,
//		both constitute end of path and will return true
static int aiMovementReachedWaypoint(	const MovementRequesterMsg* msg, 
										NavPath *path, NavPathWaypoint *wp, AIMovementBG* bg,
										AIMovementToFG *toFG, int clearBadConnections)
{
	int endOfPath = false;

	if(!devassert(wp==path->waypoints[path->curWaypoint]))
	{
		wp = path->waypoints[path->curWaypoint];
	}

	if(clearBadConnections)
	{
		if(wp->connectionToMe && wp->connectionToMe->wasBadEver)
			eaPush(&toFG->wpBadConnReset, wp);
		if(wp->beacon && wp->beacon->pathsBlockedToMe > 0)
			wp->beacon->pathsBlockedToMe--;
	}

	bg->recentMinDistToTarget = 0;
	bg->stuckCounter = 0;
	bg->timeLastShortcutCheck = 0;
	wp->dontShortcut = 0;
	wp->gotStuck = 0;
	wp->jumped = 0;
	wp->requestedRefine = 0;
	wp->attempted = 0;

	// keep the "main path" waypoints around so circle/pingpong logic
	// can use them the next time around
	if(!wp->keepWaypoint)
	{
		eaRemove(&path->waypoints, path->curWaypoint);

		// if this is anything but a ping pong path going backwards,
		// don't update current or next
		if(path->pingpongRev)
			endOfPath = navPathUpdateNextWaypoint(path);
		else
			endOfPath = (eaSize(&path->waypoints) == 0); // reached end if there are no more waypoints

		toFG->enabled = 1;

		if(globalWPParanoia)
			assert(wp->dts.dts==DTS_INBG);
		dtsSetState(&wp->dts, DTS_TOFG);

		eaPush(&toFG->wpDestroy, wp);
		mrmEnableMsgUpdatedToFG(msg);

		//if(eaSize(&path->waypoints)==0)
		//	path->curWaypoint = -1;
	}
	else
	{
		zeroVec3(wp->lastDirToPos);
		endOfPath = navPathUpdateNextWaypoint(path);
	}


	if(wp->commandsWhenPassed)
	{
		toFG->enabled = 1;
		eaPush(&toFG->wpPassed, wp);
		mrmEnableMsgUpdatedToFG(msg);
	}

	return endOfPath;
}

static void aiDoorComplete(MovementRequester* mr)
{
	AIMovementFG* fg;
	
	if(mrGetFG(mr, aiMovementMsgHandler, &fg)){
		mrEnableMsgCreateToBG(mr);
		fg->updated.doorComplete = 1;
	}
}

static F32 aiMovementGetTargetDistBG(const MovementRequesterMsg* msg, AIMovementBG* bg, const F32* targetPos, int isYCapable, int useEntIfAvailable)
{
	F32 distToMyTarget;

	S32 noColl = 0;

	// TODO: make this use the cached value from the discuss ownership calculation
	// if the capsule checks get expensive
	mrmGetNoCollBG(msg, &noColl);
	if(noColl)
	{
		Vec3 myPos;
		mrmGetPositionBG(msg, myPos);
		if(isYCapable)
			distToMyTarget = distance3(myPos, targetPos);
		else
			distToMyTarget = distance3XZ(myPos, targetPos);
	}
	else if(useEntIfAvailable && bg->orders.movementType==AI_MOVEMENT_ORDER_ENT && bg->orders.targetRef && !bg->orders.useOffset)
	{
		if(isYCapable)
			mrmGetEntityDistanceBG(msg, bg->orders.targetRef, &distToMyTarget, 0);
		else
			mrmGetEntityDistanceXZBG(msg, bg->orders.targetRef, &distToMyTarget, 0);
	}
	else
	{
		if (bg->stuckCounter > 2)
		{
			Vec3 myPos;
			F32 entDistToMyTarget;
			mrmGetPositionBG(msg, myPos);
			if(isYCapable)
			{
				mrmGetWorldCollPointDistanceBG(msg, targetPos, &distToMyTarget);
				mrmGetCapsulePointDistanceBG(msg, targetPos, &entDistToMyTarget, 0);
			}
			else
			{
				mrmGetWorldCollPointDistanceXZBG(msg, targetPos, &distToMyTarget);
				mrmGetCapsulePointDistanceXZBG(msg, targetPos, &entDistToMyTarget, 0);
			}
			MIN1(distToMyTarget, entDistToMyTarget);
		}
		else
		{
			Vec3 myPos;
			mrmGetPositionBG(msg, myPos);
			if(isYCapable)
				distToMyTarget = distance3(myPos, targetPos);
			else
				distToMyTarget = distance3XZ(myPos, targetPos);
		}
	}

	return distToMyTarget;
}

static int aiShouldAvoidPositionBG(const MovementRequesterMsg* msg, AIMovementBG* bg, const F32* pos)
{
	int i;
	Vec3 tempPos;

	if(!pos)
	{
		return false;
	}

	copyVec3(pos, tempPos);
	vecY(tempPos) += .25;

	for(i = eaSize(&bg->avoidEntries)-1; i >= 0; i--)
	{
		AIVolumeEntry* entry = bg->avoidEntries[i];
		if(aiAvoidEntryCheckPoint(NULL, tempPos, entry, false, msg))
			return true;
	}

	return false;
}

#define aiMovementRequestRefine(msg, bg, toFG, start, end, targetWp, pingpongRev, refineAgain, pathReset) \
	aiMovementRequestRefineEx(msg, bg, toFG, start, end, targetWp, pingpongRev, refineAgain, pathReset MEM_DBG_PARMS_INIT)

static void aiMovementRequestRefineEx(const MovementRequesterMsg *msg, AIMovementBG *bg, AIMovementToFG *toFG, 
									  const Vec3 start, const Vec3 end, NavPathWaypoint *targetWp, 
									  int pingpongRev, int refineAgain,
									  int pathReset MEM_DBG_PARMS)
{
	Vec3 myPos;
	toFG->enabled = 1;
	ZeroStruct(&toFG->refine);

	if(start)
	{
		toFG->refine.useRefineStartPos = 1;
		copyVec3(start, toFG->refine.refineStartPos);
	}
	else
	{
		mrmGetPositionBG(msg, myPos);
		start = myPos;
	}

	if(end || targetWp)
	{
		toFG->refine.useRefineTargetPos = 1;
		if(end)
		{
			copyVec3(end, toFG->refine.refineTarget);
		}
		else
		{
			copyVec3(targetWp->pos, toFG->refine.refineTarget);
		}
	}
	if(targetWp)
	{
		targetWp->requestedRefine = 1;
	}
	toFG->refine.refineInsertWp = targetWp;
	toFG->refine.refineAgain = !!refineAgain;
	toFG->refine.refineInsertReverse = !!pingpongRev;
	toFG->refine.requestRefine = 1;

	toFG->refine.outOfAvoidArea = aiShouldAvoidPositionBG(msg, bg, start);
	toFG->refine.pathResetOnRefine = !!pathReset;
	toFG->refine.caller_fname = caller_fname;
	toFG->refine.line = line;

	// make a backup of refine params in case we have to request again
	bg->refine = toFG->refine;

	bg->refineRequestWait = 0;
	bg->refineRequested = 1;
}

static int aiMovementShortcutCheckBG(const MovementRequesterMsg* msg, const AIMovementBG* bg,
							  const Vec3 sourcePos, const Vec3 targetPos, int avoiding)
{
	int i;
	Vec3 lineDir;
	F32 lineLen;
	WorldColl* wc;

	PERFINFO_AUTO_START_FUNC();

	if(	!bg->config.collisionlessMovement &&
		mrmGetWorldCollBG(msg, &wc) &&
		aiCollideRayWorldColl(wc, NULL, sourcePos, NULL, targetPos, STD_AICOLLIDERAY_FLAGS(bg->flying, false)))
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	subVec3(targetPos, sourcePos, lineDir);
	lineLen = lengthVec3(lineDir);
	normalVec3(lineDir);

	if(!avoiding)
	{
		for(i = eaSize(&bg->avoidEntries)-1; i >= 0; i--)
		{
			AIVolumeEntry* entry = bg->avoidEntries[i];
			if(aiAvoidEntryCheckLine(NULL, sourcePos, targetPos, entry, false, msg))
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
	}
	PERFINFO_AUTO_STOP();
	return true;
}

static int aiMovementCheckShortcutWaypoint(const MovementRequesterMsg* msg, AIMovementBG* bg,
									AIMovementToFG* toFG, Vec3 sourcePos,
									NavPathWaypoint* targetWp, int pingpongRev)
{
	if(!aiMovementShortcutCheckBG(msg, bg, sourcePos, targetWp->pos, targetWp->avoiding))
	{
		aiMovementRequestRefine(msg, bg, toFG, sourcePos, NULL, targetWp, pingpongRev, 1, 0);
		return true;
	}

	return false;
}

static void aiMovementUpdateTargetWp(AIMovementBG *bg, NavPath* path, Vec3 targetPos)
{
	NavPathWaypoint* targetWp = NULL;

	if(eaSize(&path->waypoints))
		targetWp = path->waypoints[eaSize(&path->waypoints)-1];

	if(!targetWp || !targetWp->targetWp)
	{
		targetWp = createNavPathWaypoint();
		targetWp->connectType = NAVPATH_CONNECT_ATTEMPT_SHORTCUT;
		targetWp->targetWp = 1;
		navPathAddTail(path, targetWp);

		dtsSetState(&targetWp->dts, DTS_INBG);

		path->curWaypoint = 0;
	}
	else
		devassert(targetWp->targetWp);

	if(path->curWaypoint == eaSize(&path->waypoints)-1 && !sameVec3(targetPos, targetWp->pos))
		bg->recentMinDistToTarget = distance3(bg->lastPos, targetPos);

	copyVec3(targetPos, targetWp->pos);

	if(!targetWp->gotStuck && !targetWp->jumped && !targetWp->dontShortcut && !targetWp->requestedRefine)
		copyVec3(targetPos, targetWp->lastFailPos);
	else if(distance3Squared(targetPos, targetWp->lastFailPos) > SQR(5))
	{
		copyVec3(targetPos, targetWp->lastFailPos);
		targetWp->gotStuck = 0;
		targetWp->jumped = 0;
		targetWp->dontShortcut = 0;
		targetWp->requestedRefine = 0;
	}
}

static void aiMovementRefineToNextWp(const MovementRequesterMsg *msg, AIMovementBG* bg, AIMovementToFG* toFG)
{
	NavPath* path = &bg->path;
	NavPathWaypoint *wp = NULL;

	if(path->curWaypoint >= 0 && path->curWaypoint < eaSize(&path->waypoints))
	{
		aiMovementRequestRefine(msg, bg, toFG, NULL, NULL, path->waypoints[path->curWaypoint], path->pingpongRev, 0, 0);
	}
	else if(bg->orders.movementType!=AI_MOVEMENT_ORDER_NONE)
	{
		aiMovementRequestRefine(msg, bg, toFG, NULL, bg->orders.targetPos, NULL, 0, 0, 0);
	}
	else
	{
		aiMovementRequestRefine(msg, bg, toFG, NULL, bg->targetPos, NULL, 0, 0, 0);
	}
}

static void aiMovementExpireToWp(const MovementRequesterMsg* msg, AIMovementBG* bg, AIMovementToFG* toFG, NavPathWaypoint* wp, const char* reason)
{
	NavPath* path = &bg->path;

	while(path->curWaypoint < eaSize(&path->waypoints) &&
		path->curWaypoint >= 0 && path->waypoints[path->curWaypoint] != wp)
	{
		AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 5,
							"Expiring waypoint at " LOC_PRINTF_STR " due to %s",
							vecParamsXYZ(path->waypoints[path->curWaypoint]->pos), reason);
		aiMovementReachedWaypoint(msg, path, path->waypoints[path->curWaypoint], bg, toFG, false);
	}
}

static NavPathWaypoint* aiMovementGetNextShortcutWp(NavPath* path)
{
	int curWp = path->curWaypoint;
	int pingpongRev = path->pingpongRev;

	while(curWp < eaSize(&path->waypoints) && curWp >= 0 &&
			path->waypoints[curWp]->connectType != NAVPATH_CONNECT_ATTEMPT_SHORTCUT)
	{
		navPathGetNextWaypoint(path, &curWp, &pingpongRev);
	}

	if(curWp >= 0 && curWp < eaSize(&path->waypoints))
		return path->waypoints[curWp];
	else
		return NULL;
}


static void aiMovementRefineOutOfAvoid(	SA_PARAM_NN_VALID const MovementRequesterMsg* msg, 
										SA_PARAM_NN_VALID AIMovementBG* bg, 
										SA_PARAM_NN_VALID AIMovementToFG* toFG, 
										SA_PARAM_OP_VALID NavPathWaypoint* targetWp)
{
	// refine with no target will refine to an escape beacon,
	aiMovementRequestRefine(msg, bg, toFG, NULL, NULL, NULL, 0, 0, 0);
	if (targetWp)
	{	// but just in case there isn't a escape beacon, set the refine target
		copyVec3(targetWp->pos, toFG->refine.refineTarget);
	}
}

static void aiMovementResolveAvoid(const MovementRequesterMsg* msg, AIMovementBG* bg, AIMovementToFG* toFG)
{
	Vec3 curPos;
	bool bInAvoid;
	NavPathWaypoint* targetWp = navPathGetTargetWaypoint(&bg->path);
	mrmGetPositionBG(msg, curPos);
	bInAvoid = aiShouldAvoidPositionBG(msg, bg, curPos);
	if(targetWp && !bInAvoid)
	{
		NavPathWaypoint* wp = aiMovementGetNextShortcutWp(&bg->path);
		aiMovementExpireToWp(msg, bg, toFG, wp, "new avoid volume");
		if(!aiShouldAvoidPositionBG(msg, bg, wp->pos))
		{
			aiMovementRefineToNextWp(msg, bg, toFG);
		}
	}
	else // just get out of the avoid area
	{
		if (bInAvoid)
		{
			aiMovementRefineOutOfAvoid(msg, bg, toFG, targetWp);
		}
		else
		{
			// we are trying to get around an avoid, but we aren't in it
			aiMovementRequestRefine(msg, bg, toFG, NULL, bg->targetPos, NULL, 0, 0, 0);
		}
	}
}

static void aiMovementTargetPosToPYR(const AIMovementBG* bg, const Vec3 curPos, const Vec3 targetPos, Vec3 outPyr)
{
	Vec3 faceDir;
	
	subVec3(targetPos, curPos, faceDir);

	if(bg->config.pitchWhenMoving && ABS(faceDir[1]) > 2.f)
	{
		Vec3 pitchDir;
		copyVec3(faceDir, pitchDir);
		pitchDir[2] = ABS(pitchDir[2]);
		outPyr[0] = getVec3Pitch(pitchDir);

		if(g_CombatConfig.fFlightPitchClamp){
			F32 p = RAD(g_CombatConfig.fFlightPitchClamp);
			outPyr[0] = CLAMPF32(outPyr[0], -p, p);
		}else{
			outPyr[0] = CLAMPF32(outPyr[0], -MAX_FLIGHT_PITCH, MAX_FLIGHT_PITCH);
		}
	} else {
		outPyr[0] = 0;
	}

	if (vec3IsZeroXZ(faceDir)) {
		// set yaw to current faceRot instead
		Vec3 pyrTmp;
		quatToPYR(bg->faceRot, pyrTmp);
		outPyr[1] = pyrTmp[1];
	} else  {
		outPyr[1] = getVec3Yaw(faceDir);
	}

	outPyr[2] = 0;
}

static void aiMovementTargetPosToRot(const AIMovementBG* bg, const Vec3 curPos, const Vec3 targetPos, Quat outRot)
{
	if (!bg->config.dontRotate)
	{
		Vec3 facePyr;
		aiMovementTargetPosToPYR(bg, curPos, targetPos, facePyr);
		PYRToQuat(facePyr, outRot);
	}
}


static int aiMovementWantRotation(const MovementRequesterMsg* msg, const AIMovementBG* bg)
{
	if(bg->rotation.type && !bg->config.dontRotate)
	{
		Quat targetRot;
		Vec3 targetPYR;
		bool bHasPYR = false;
		
		switch(bg->rotation.type)
		{
		xcase AI_MOVEMENT_ROTATION_NONE:
			devassert(0);
		xcase AI_MOVEMENT_ROTATION_POS:
		{
			Vec3 curPos;
			
			if(!mrmGetPositionBG(msg, curPos))
				devassert(0);
			
			aiMovementTargetPosToPYR(bg, curPos, bg->rotation.finalFacePos, targetPYR);
			bHasPYR = true;
		}
		xcase AI_MOVEMENT_ROTATION_ROT:
			copyQuat(bg->rotation.finalFaceRot, targetRot);
			// don't calculate the PYR because we probably don't care for this rotation type
			// and calculating the PYR of a quat is slow enough

		xcase AI_MOVEMENT_ROTATION_ENTREF:
		{
			Vec3 curPos;
			Vec3 targetPos;

			if(!mrmGetEntityPositionBG(msg, bg->rotation.finalFaceEntRef, targetPos))
				return false;

			if(!mrmGetPositionBG(msg, curPos))
				devassert(0);

			aiMovementTargetPosToPYR(bg, curPos, targetPos, targetPYR);
			bHasPYR = true;
		}
		xdefault:
			devassert(0);
		}

		if (bHasPYR)
		{
			Vec2 curPYFace;
			F32 yawDiff;

			if (!mrmGetFacePitchYawBG(msg, curPYFace))
				devassert(0);

			// only checking the yaw difference for now, we may want to check pitch as well 
			// when bg->config.pitchWhenMoving is set
			yawDiff = subAngle(curPYFace[1], targetPYR[1]);
			if (ABS(yawDiff) > RAD(5))
			{	// the actual facing of the entity does not line up with the rotation
				return true;
			}

			// for now, I'm assuming that if we are given a PYR the quaternion has not been calculated.
			// if we need to get the PYR for AI_MOVEMENT_ROTATION_ROT, then this will need to change
			PYRToQuat(targetPYR, targetRot);
		}
		

		{
			Quat curRot;
			if(!mrmGetRotationBG(msg, curRot))
				devassert(0);

			return !quatWithinAngle(curRot, targetRot, RAD(5));
		}
		
	}

	return false;
}

static int disableAIMovement = false;
AUTO_CMD_INT(disableAIMovement, disableAIMovement);

static int disableTeleport = false;
AUTO_CMD_INT(disableTeleport, noAITP);

extern ParseTable parse_AIDebugWaypoint[];
#define TYPE_parse_AIDebugWaypoint AIDebugWaypoint

static void aiMovementSetupTargetOptions(const MovementRequesterMsg* msg, AIMovementBG* bg, F32 throttlePercentage)
{
	if(bg->config.overrideMovementTurnRate!=-1)
		mrmTargetSetTurnRateAsOverrideBG(msg, bg->config.overrideMovementTurnRate);
	else
		mrmTargetSetTurnRateAsNormalBG(msg);

	if(bg->config.overrideMovementFriction)
		mrmTargetSetFrictionAsOverrideBG(msg, bg->config.overrideMovementFriction);
	else
		mrmTargetSetFrictionAsNormalBG(msg);

	if(bg->config.overrideMovementTraction)
		mrmTargetSetTractionAsOverrideBG(msg, bg->config.overrideMovementTraction);
	else
		mrmTargetSetTractionAsNormalBG(msg);

	if(bg->overrideSpeed)
		mrmTargetSetSpeedAsOverrideBG(msg, bg->overrideSpeed);
	else if(bg->config.minimumThrottlePercentage)
	{
		F32 maxSpeed;
		if(mrmGetMaxSpeedBG(msg, &maxSpeed))
			mrmTargetSetMinimumSpeedBG(msg, bg->config.minimumThrottlePercentage/100 * maxSpeed);
	}
	else if(throttlePercentage)
	{
		F32 maxSpeed;
		if(mrmGetMaxSpeedBG(msg, &maxSpeed))
			mrmTargetSetMinimumSpeedBG(msg, throttlePercentage/100 * maxSpeed);
	}
	else
		mrmTargetSetSpeedAsNormalBG(msg);
}

static void aiMovementSetTargetAsStoppedBG(const MovementRequesterMsg* msg, AIMovementBG* bg)
{
	mrmTargetSetAsStoppedBG(msg);

	aiMovementSetupTargetOptions(msg, bg, 0);
}

static void aiMovementSetTargetPosBG(const MovementRequesterMsg* msg, AIMovementBG* bg, const Vec3 pos, F32 throttlePercentage)
{
	Vec3 tmp;

	copyVec3(pos, tmp);
	tmp[1] -= 2;
	mrmTargetSetAsPointBG(msg, tmp);

	aiMovementSetupTargetOptions(msg, bg, throttlePercentage);

	if(!sameVec3(pos, bg->targetPosLastFrame))
		bg->recentMinDistToTarget = 0;

	// Store pos for stuck detection
	copyVec3(pos, bg->targetPosLastFrame);
	bg->triedMovingLastFrame = true;
}

static __forceinline F32 aiMovementGetDistThreshold(const MovementRequesterMsg* msg, AIMovementBG *bg)
{
	F32 maxSpeed = 0;
	mrmGetMaxSpeedBG(msg, &maxSpeed);

	if (bg->orders.movementType == AI_MOVEMENT_ORDER_ENT &&
		bg->orders.entDetail == AI_MOVEMENT_ORDER_ENT_FOLLOW)
	{
		NavPathWaypoint *wp = eaGet(&bg->path.waypoints, bg->path.curWaypoint);

		if(wp && wp->targetWp)
			return 4;		// 4ft allows follow to not crowd the target
	}

	if(bg->orders.movementType==AI_MOVEMENT_ORDER_ENT)
	{
		if(bg->orders.entDetail == AI_MOVEMENT_ORDER_ENT_PATROL_OFFSET)
			return maxSpeed * MM_SECONDS_PER_STEP * 3;
		if(bg->orders.entDetail == AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET)
			return maxSpeed * MM_SECONDS_PER_STEP * 3;
	}

	if(bg->updateToFG.movementCompleted)
		return maxSpeed * MM_SECONDS_PER_STEP * 4;

	if(bg->stuckCounter<=3)
		return maxSpeed * MM_SECONDS_PER_STEP * pow(0.5, 3 - bg->stuckCounter);
	else
		return maxSpeed * MM_SECONDS_PER_STEP * 4;
}

static void aiMovementHandleDiscussDataOwnership(const MovementRequesterMsg *msg, AIMovementBG *bg, AIMovementToFG *toFG)
{
	Vec3 curPos;
	Vec3 targetPos;
	F32 distToTarget;
	U32 remainingWaypoints;
	U32 isYCapable;
	F32 distThreshold = aiMovementGetDistThreshold(msg, bg);
	NavPathWaypoint *curWp;

	toFG->id = bg->id;
	curWp = eaGet(&bg->path.waypoints, bg->path.curWaypoint); 

	if(bg->rotationDisabled)
	{
		mrmAcquireDataOwnershipBG(msg, MDC_BIT_ROTATION_CHANGE, 0, NULL, NULL);
	}
	else
	{
		mrmReleaseDataOwnershipBG(msg, MDC_BIT_ROTATION_CHANGE);
	}
	
	if(msg->in.bg.discussDataOwnership.flags.isDuringCreateOutput){
		return;
	}

	if(bg->debugForceProcess)
	{
		mrmAcquireDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET, 0, NULL, NULL);
		mrmEnableMsgUpdatedToFG(msg);
	}

	if(bg->sleeping)
		return;
		
	remainingWaypoints = eaSize(&bg->path.waypoints);
	isYCapable =	//mrmIsKinematicBG(msg) || MS: Do something with this.
					bg->flying || bg->canFly;

	if(aiMovementWantRotation(msg, bg)) {
		U32 acquiredBits;
		if (mrmAcquireDataOwnershipBG(msg, MDC_BIT_ROTATION_TARGET, 0, &acquiredBits, NULL)) {
			if (acquiredBits & MDC_BIT_ROTATION_TARGET) {
				mrmGetRotationBG(msg, bg->faceRot);
			}
		}
	}

	if(ea32Size(&bg->animHold) || bg->animHoldClear)
		mrmAcquireDataOwnershipBG(msg, MDC_BIT_ANIMATION, 0, NULL, NULL);

	if(bg->config.immobile)
	{
		bg->updateToFG.movementCompleted = 1;
		toFG->updateToFG.movementCompleted = bg->updateToFG.movementCompleted;
		toFG->updateToFG.updated.movementCompleted = true;
		mrmEnableMsgUpdatedToFG(msg);

		AI_DEBUG_PRINT_TAG_BG(AI_LOG_MOVEMENT, 4, AIMLT_MOVCOM, "%s:%d - UpdateToFG: %d", __FILE__, __LINE__, bg->updateToFG.movementCompleted);
		return;
	}

	mrmGetPositionBG(msg, curPos);

	curPos[1]+=0.1;
	if(	bg->flying && !bg->config.alwaysFly && !bg->alwaysFlying && 
		(!curWp || curWp->connectType!=NAVPATH_CONNECT_FLY))
	{	
		WorldColl* wc = NULL;
		F32 groundDist;
		mrmGetWorldCollBG(msg, &wc);
		groundDist = aiFindGroundDistance(wc, curPos, NULL);
		if (groundDist >= 0.f && groundDist < 2.f)	
		{
			// If I'm on the ground for .5s for non fly conns
			if(!bg->flyingOnGround)
			{
				bg->timeLastGroundCheck = ABS_TIME;
				bg->flyingOnGround = 1;
			}
			else if(ABS_TIME_SINCE(bg->timeLastGroundCheck)>SEC_TO_ABS_TIME(0.5))
			{
				bg->shouldFly = 0;
				toFG->turnOffFlight = 1;
				mrmEnableMsgUpdatedToFG(msg);
			}
		}
	}
	else
	{
		bg->flyingOnGround = 0;
	}

	if(!bg->doMove)
	{
		if(!bg->doMoveDone)
		{
			bg->doMoveDone = 1;
			bg->updateToFG.movementCompleted = 1;
			toFG->updateToFG.movementCompleted = bg->updateToFG.movementCompleted;
			toFG->updateToFG.updated.movementCompleted = true;
			mrmEnableMsgUpdatedToFG(msg);

			AI_DEBUG_PRINT_TAG_BG(AI_LOG_MOVEMENT, 6, AIMLT_MOVCOM, "%s:%d - UpdateToFG: %d", __FILE__, __LINE__, bg->updateToFG.movementCompleted);
		}
		return;
	}

	if(bg->orders.movementType==AI_MOVEMENT_ORDER_ENT && bg->orders.targetRef)
	{
		// If the critter is trying to get out of an avoid volume, he should give up on his movement order.  I don't know if this is
		// the ideal place to do that, but most of the code that decides to clear the order seems to be here.  I'm not sure why he doesn't
		// just get a different order when he does the pathfind.  [RMARR - 2/16/13]
		if (curWp && curWp->avoiding)
		{
			bg->orders.movementType = AI_MOVEMENT_ORDER_NONE;
			bg->orders.targetRef = 0;
		}
		else
		{
			if(mrmGetEntityPositionBG(msg, bg->orders.targetRef, targetPos))
			{
				F32 fTargetDistance = 0.f;
						
				if (bg->reachedOffset && bg->orders.entDetail == AI_MOVEMENT_ORDER_ENT_COMBAT_MOVETO_OFFSET)
				{
					// combat melee movement- if we've reached our offset, periodically check if we need to start moving again.
					distThreshold = FLT_MAX;
				
					if (!bg->spcTimeLastCheckedCombatOffset)
					{
						mrmGetProcessCountBG(msg, &bg->spcTimeLastCheckedCombatOffset);
					}
					else if (mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcTimeLastCheckedCombatOffset, 0.25f) &&
								mrmGetEntityDistanceBG(msg, bg->orders.targetRef, &fTargetDistance, false))
					{
						bool bNeedsMove = false;

						F32 fTargetDistSQR = lengthVec3Squared(bg->orders.targetOffset);
						F32 fDistDiffSQRAbs = SQR(fTargetDistance) - fTargetDistSQR;

						mrmGetProcessCountBG(msg, &bg->spcTimeLastCheckedCombatOffset);

						// check the actual offset distance only once every few seconds.
						if (++bg->combatOffsetCheckCounter > 12)
							bg->combatOffsetCheckCounter = 0;
						
						if(bg->orders.offsetRotRelative)
						{
							Quat q;
							Vec3 pyr;

							mrmGetEntityFacePitchYawBG(msg, bg->orders.targetRef, pyr);
							yawQuat(-pyr[1], q);
							quatRotateVec3(q, bg->orders.targetOffset, pyr);
							addVec3(pyr, targetPos, targetPos);
						}
						else
						{
							addVec3(targetPos, bg->orders.targetOffset, targetPos);
						}

						if (fDistDiffSQRAbs > SQR(2.5f) ||
							(!bg->combatOffsetCheckCounter && distance3SquaredXZ(targetPos, curPos) > SQR(6.f)))
						{
							bg->reachedOffset = false;
							bg->spcTimeLastCheckedCombatOffset = 0;

							distThreshold = 0.f;
							
							if (gConf.bNewAnimationSystem) {
								targetPos[1] += bg->config.movementYOffset + 2.f;
							} else {
								targetPos[1] += 2;  // Standard 2ft offset
							}

							//MM_CHECK_DYNPOS_DEVONLY(targetPos);
							copyVec3(targetPos, bg->targetPos);
							aiMovementUpdateTargetWp(bg, &bg->path, targetPos);
						}
					}
				}
				else if (bg->orders.stopWithinRange && 
							mrmGetEntityDistanceBG(msg, bg->orders.targetRef, &fTargetDistance, false) && 
							SQR(fTargetDistance) < lengthVec3Squared(bg->orders.targetOffset))
				{
					// we're close enough, we're done.
					bg->orders.movementType = AI_MOVEMENT_ORDER_NONE;
					bg->orders.targetRef = 0;
					bg->doMove = false;
					return;
				}
				else
				{
					Vec3 vel;
					if (bg->orders.settled)
					{
						// If we are settled, use the settled position instead of the entity's position
						copyVec3(bg->orders.settledPos, targetPos);
					}

					copyVec3(bg->targetEntPos, bg->lastTargetEntPos);
					copyVec3(targetPos, bg->targetEntPos);
				

					subVec3(bg->targetEntPos, bg->lastTargetEntPos, vel);
					scaleVec3(vel,MM_STEPS_PER_SECOND,vel);
					interpVec3(0.75, vel, bg->targetVelAvg, bg->targetVelAvg);

					if(!vec3IsZero(vel))
						bg->reachedOffset = 0;
				
					if(bg->orders.useOffset)
					{
						Vec3 groundPos;
						Vec3 tmpPos;
						WorldColl* wc = NULL;
						if(bg->orders.offsetRotRelative)
						{
							Quat q;
							Vec3 pyr;

							mrmGetEntityFacePitchYawBG(msg, bg->orders.targetRef, pyr);
							yawQuat(-pyr[1], q);
							quatRotateVec3(q, bg->orders.targetOffset, pyr);
							addVec3(pyr, targetPos, targetPos);
						}
						else
							addVec3(targetPos, bg->orders.targetOffset, targetPos);

						copyVec3(targetPos, tmpPos);
						tmpPos[1] += 10;
						mrmGetWorldCollBG(msg, &wc);
						if(aiFindGroundDistance(wc, tmpPos, groundPos)!=-FLT_MAX)
						{
							//I'm guessing this code was originally written with the assumption that both the target and active entity would be on the ground
							if (gConf.bNewAnimationSystem &&
								bg->flying &&
								fabsf(bg->config.movementYOffset) > 0.000001f)
							{
								targetPos[0] = groundPos[0];
								targetPos[1] = MAX(targetPos[1],groundPos[1]-0.01);
								targetPos[2] = groundPos[2];
							}
							else
							{
								copyVec3(groundPos, targetPos);
								targetPos[1] -= 0.01;
							}
						}
					}

					if (gConf.bNewAnimationSystem) {
						targetPos[1] += bg->config.movementYOffset + 2;
					} else {
						targetPos[1] += 2;  // Standard 2ft offset
					}

					//MM_CHECK_DYNPOS_DEVONLY(targetPos);
					copyVec3(targetPos, bg->targetPos);
					aiMovementUpdateTargetWp(bg, &bg->path, targetPos);
				}
			}
			else
			{	// could not find the entity we were told to follow- clear our movement
				bg->orders.movementType = AI_MOVEMENT_ORDER_NONE;
				bg->orders.targetRef = 0;
				bg->doMove = false;
				return;
			}
		}
	}
	else
	{
		if(bg->orders.movementType!=AI_MOVEMENT_ORDER_NONE) {
			//MM_CHECK_DYNPOS_DEVONLY(bg->orders.targetPos);
			copyVec3(bg->orders.targetPos, bg->targetPos);
		} else if(bg->path.curWaypoint>=0 && bg->path.curWaypoint<eaSize(&bg->path.waypoints)) {
			//MM_CHECK_DYNPOS_DEVONLY(bg->path.waypoints[bg->path.curWaypoint]->pos);
			copyVec3(bg->path.waypoints[bg->path.curWaypoint]->pos, bg->targetPos);
		}

		copyVec3(bg->targetPos, targetPos);
	}
	
	distToTarget = 0;
	if(	distThreshold != FLT_MAX && 
		!bg->config.continuousCombatMovement)
	{
		if(curWp && curWp->avoiding)
			copyVec3(curWp->pos, targetPos);
		else if(bg->orders.movementType!=AI_MOVEMENT_ORDER_NONE)
			copyVec3(bg->targetPos, targetPos);
		else if(remainingWaypoints && (bg->path.circular || bg->path.pingpong))
			distToTarget = FLT_MAX;
		else if(remainingWaypoints)
			copyVec3(bg->path.waypoints[remainingWaypoints-1]->pos, targetPos);

		if(targetPos)
		{
			if (gConf.bNewAnimationSystem) {
				//movementYOffset should now be part of the target
				targetPos[1] -= 2.0;
			} else {
				targetPos[1] += bg->config.movementYOffset - 2.0;
			}

			distToTarget = aiMovementGetTargetDistBG(msg, bg, targetPos, isYCapable, true);
		}
	}

	if(bg->config.continuousCombatMovement || distToTarget > distThreshold)
	{
		int needsBits = true;
		
		if(bg->orders.movementType==AI_MOVEMENT_ORDER_ENT &&
			bg->orders.useOffset &&
			bg->reachedOffset)
		{
			needsBits = false;
		}
		
		if(needsBits)
		{
			U32 acquiredBits;
			if (mrmAcquireDataOwnershipBG(msg, MDC_BITS_TARGET_ALL, 0, &acquiredBits, NULL)) {
				if (acquiredBits & MDC_BIT_ROTATION_TARGET) {
					mrmGetRotationBG(msg, bg->faceRot);
				}
			}
			bg->updateToFG.movementCompleted = false;
			toFG->updateToFG.movementCompleted = false;
			toFG->updateToFG.updated.movementCompleted = true;
			mrmEnableMsgUpdatedToFG(msg);

			AI_DEBUG_PRINT_TAG_BG(AI_LOG_MOVEMENT, 6, AIMLT_MOVCOM, "%s:%d - UpdateToFG: %d", __FILE__, __LINE__, bg->updateToFG.movementCompleted);
		}
	}
	else if(bg->orders.movementType==AI_MOVEMENT_ORDER_ENT && 
				bg->orders.useOffset && 
				bg->updateToFG.movementCompleted)
	{
		bg->reachedOffset = true;
	}

	// if we haven't reached our offset doing our combat movement
	if (!bg->reachedOffset && 
		bg->orders.entDetail == AI_MOVEMENT_ORDER_ENT_COMBAT_MOVETO_OFFSET)
	{
		// if it's taking too long to get to our offset, but we're in range for a period of time, just stop for now
		F32 fDistanceToTarget = 0.f;
		
		// only check periodically 
		if (!bg->spcTimeLastCheckedCombatOffset)
		{
			mrmGetProcessCountBG(msg, &bg->spcTimeLastCheckedCombatOffset);
			bg->spcTimeInRangeOfCombatOffset = 0;
		}
		else if (mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcTimeLastCheckedCombatOffset, 0.25f) && 
					mrmGetEntityDistanceBG(msg, bg->orders.targetRef, &fDistanceToTarget, false))
		{
			F32 fTargetDistSQR = lengthVec3Squared(bg->orders.targetOffset);
			
			if (SQR(fDistanceToTarget) < fTargetDistSQR)
			{
				if (!bg->spcTimeInRangeOfCombatOffset)
				{
					mrmGetProcessCountBG(msg, &bg->spcTimeInRangeOfCombatOffset);
				}
				else if (mrmProcessCountPlusSecondsHasPassedBG(msg, bg->spcTimeInRangeOfCombatOffset, 1.75f))
				{	// we've been in range for enough time- stop moving
					bg->reachedOffset = true;
					bg->combatOffsetCheckCounter = 0;
					bg->spcTimeLastCheckedCombatOffset = 0;

					// 
					bg->updateToFG.movementCompleted = true;
					toFG->updateToFG.movementCompleted = true;
					toFG->updateToFG.updated.movementCompleted = true;
					mrmEnableMsgUpdatedToFG(msg);
					AI_DEBUG_PRINT_TAG_BG(AI_LOG_MOVEMENT, 4, AIMLT_MOVCOM, "%s:%d - UpdateToFG: %d", __FILE__, __LINE__, bg->updateToFG.movementCompleted);

					mrmReleaseDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET);
				}
			}
			else
			{
				bg->spcTimeInRangeOfCombatOffset = 0;
			}
		}
	}
	//


	if(bg->doMove)
	{
		int needsBits = true;
		if(bg->orders.movementType==AI_MOVEMENT_ORDER_ENT &&
			bg->orders.useOffset &&
			bg->reachedOffset)
		{
			needsBits = false;
		}

		if(needsBits) {
			U32 acquiredBits;
			if (mrmAcquireDataOwnershipBG(msg, MDC_BITS_TARGET_ALL, 0, &acquiredBits, NULL)) {
				if (acquiredBits & MDC_BIT_ROTATION_TARGET) {
					mrmGetRotationBG(msg, bg->faceRot);
				}
			}
		}
	}

	if(bg->doTeleport)
		mrmAcquireDataOwnershipBG(msg, MDC_BIT_POSITION_CHANGE, 0, NULL, NULL);
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int aiMovementCheckAvoids(const MovementRequesterMsg *msg, 
												  AIMovementBG *bg, 
												  AIMovementToFG *toFG, 
												  NavPath* path,
												  const Vec3 curPos,
												  S32 *avoidingTargetOut)
{
	S32 avoidingTarget = false;
	Vec3 avoidTestStart;
	Vec3 avoidTestEnd;
	S32 i;

	NavPathWaypoint *curWp = eaGet(&path->waypoints, path->curWaypoint);

	copyVec3(curPos, avoidTestStart);
	//avoidTestStart[1] += 2; // this seems to be wrong.  It looks like curPos already has this

	if(curWp && curWp->avoiding)
		return false;

	for(i = eaSize(&bg->avoidEntries)-1; i >= 0; i--)
	{
		AIVolumeEntry* entry = bg->avoidEntries[i];

		if(aiAvoidEntryCheckPoint(NULL, avoidTestStart, entry, false, msg))
		{
			NavPathWaypoint* targetWp;
			AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 2,
								"PATHING: aiAvoidEntryCheckPoint found an avoid volume. "
								LOC_PRINTF_STR
								" Resolving Avoid.",
								vecParamsXYZ(avoidTestStart));

			targetWp = navPathGetTargetWaypoint(&bg->path);
			aiMovementRefineOutOfAvoid(msg, bg, toFG, targetWp);
			return true;
		}

		if(aiAvoidEntryCheckPoint(NULL, bg->targetPos, entry, false, msg))
		{
			avoidingTarget = true;
			if(avoidingTargetOut)
				*avoidingTargetOut = true;
		}
	}
	
	// Check cur position and line to next waypoint for any avoid stuff that might've popped up
	// recently (or is on an entity that's moving), and make sure we don't teleport there

	// actually, if the AI is trapped, we DO want him to teleport there.  This should only happen if he is stuck.
	// We should not have to return true from this function, which will short circuit the stuck checking.  We probably
	// do want to go ahead and call aiMovementResolveAvoid [RMARR - 5/9/13]
	for(i = eaSize(&bg->avoidEntries)-1; i >= 0; i--)
	{
		AIVolumeEntry* entry = bg->avoidEntries[i];
		if(curWp)
		{
			copyVec3(curWp->pos, avoidTestEnd);

			if(aiAvoidEntryCheckLine(NULL, avoidTestStart, avoidTestEnd, entry, false, msg))
			{
				if(!avoidingTarget)
					aiMovementResolveAvoid(msg, bg, toFG);
				return false;
			}
		}
		else
		{
			copyVec3(bg->targetPos, avoidTestEnd);
			if(aiAvoidEntryCheckLine(NULL, avoidTestStart, avoidTestEnd, entry, false, msg))
			{
				if(!avoidingTarget)
					aiMovementResolveAvoid(msg, bg, toFG);
				return false;
			}
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int _checkReachable(const MovementRequesterMsg *msg, 
										 AIMovementBG *bg, 
										 AIMovementToFG *toFG, 
										 NavPath* path,
										 const Vec3 curPos,
										 U32 isYCapable)
{
	NavPathWaypoint *curWp = eaGet(&path->waypoints, path->curWaypoint);
	
	// Check to see if our target is actually reachable
	if(ABS_TIME_SINCE(bg->timeLastReachableCheck) > SEC_TO_ABS_TIME(0.25))
	{
		bg->targetUnreachable = 0;
		if(curWp && curWp->targetWp && !isYCapable)
		{
			S32 targetOnGround = 0;
			F32 groundDist = 0;
			bg->timeLastReachableCheck = ABS_TIME;

			if(!bg->orders.targetRef || mrmGetEntityOnGroundBG(msg, bg->orders.targetRef, &targetOnGround))
			{
				if(!targetOnGround)
				{
					WorldColl* wc = NULL;
					mrmGetWorldCollBG(msg, &wc);
					groundDist = worldGetPointFloorDistance(wc, curWp->pos, 5, 10, &targetOnGround);

					targetOnGround = targetOnGround || groundDist > -3;
				}

				if(targetOnGround)
				{
					//removed y+=2 that was previously here since this was adding it a 2nd time after one of the calls on the stack
					if (!aiMovementShortcutCheckBG(msg, bg, curPos, curWp->pos, curWp->avoiding))
					{
						targetOnGround = 0;
					}
				}
			}

			if(!targetOnGround)
				bg->targetUnreachable = 1;
		}
	}

	if(bg->targetUnreachable && curWp && curWp->connectType==NAVPATH_CONNECT_ATTEMPT_SHORTCUT)
	{
		aiMovementRequestRefine(msg, bg, toFG, NULL, bg->targetPos, NULL, 0, 0, 0);
		return true;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int aiMovementDoStuckDetection(const MovementRequesterMsg *msg, 
													AIMovementBG *bg, 
													AIMovementToFG *toFG, 
													NavPath* path,
													const Vec3 curPos)
{
	F32 curDistToTarget = distance3(curPos, bg->targetPosLastFrame);
	F32 speed = 0;
	Vec3 vel;
	Vec3 dir;
	F32 velDiff;
	F32 velMoved = 0;
	F32 turnRate = 0;
	F32 distMoved;
	S32 onGround = true;

	if(!bg->triedMovingLastFrame)
		return false;

	// If we're jumping or falling or knocked, don't do stuck checks
	if(mrmGetOnGroundBG(msg, &onGround, NULL) && !onGround && !bg->flying)
		return false;

	mrmGetVelocityBG(msg, vel);
	normalVec3(vel);

	subVec3(bg->targetPosLastFrame, curPos, dir);
	normalVec3(dir);

	velDiff = dotVec3(vel, dir);

	if(!bg->recentMinDistToTarget)
	{
		bg->recentMinDistToTarget = distance3(bg->lastPos, bg->targetPosLastFrame);
		bg->recentMaxVelDotToTarget = -1;
	}

	copyVec3(curPos, bg->lastPos); // Was able to hit a break point when the entity was frozen in place BUT this kept showing a movement distance, when that happened distMoved was computed with the else clause below here

	if(curDistToTarget > bg->recentMinDistToTarget)
	{
		distMoved = curDistToTarget - bg->recentMinDistToTarget;
		if(distMoved > speed * MM_SECONDS_PER_STEP)
			distMoved = 0;
	}
	else
	{
		distMoved = bg->recentMinDistToTarget - curDistToTarget;
		bg->recentMinDistToTarget = curDistToTarget;
	}

	mrmGetMaxSpeedBG(msg, &speed);

	if(velDiff < bg->recentMaxVelDotToTarget)
		velMoved = 0;
	else if(speed)
	{
		velMoved = velDiff - bg->recentMaxVelDotToTarget;
		bg->recentMaxVelDotToTarget = velDiff;
	}


	AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 7,
						"distMoved: %.2f, curDistToTarget: %.2f, recentMin: %.2f",
						distMoved, curDistToTarget, bg->recentMinDistToTarget);

	mrmGetTurnRateBG(msg, &turnRate);
	if (distMoved < speed*MM_SECONDS_PER_STEP*0.001 && 
		(	!turnRate && velMoved < 0.001 || 
			turnRate && velMoved < turnRate*MM_SECONDS_PER_STEP*0.001))
	{
		bg->stuckCounter++;

		if((bg->stuckCounter > 10 && !bg->refineRequested) || bg->stuckCounter > 80)
		{
			S32 i;
			int nextWp = path->curWaypoint;
			U32 pingpongRev = path->pingpongRev;
			int dontExpire = false;

			AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 5,
								"Stuck at " LOC_PRINTF_STR " heading for " LOC_PRINTF_STR " so requesting a new path",
								vecParamsXYZ(curPos),
								vecParamsXYZ(bg->targetPos));

			if(path->curWaypoint >= 0 && path->curWaypoint < eaSize(&path->waypoints))
			{
				NavPathWaypoint* wp = path->waypoints[path->curWaypoint];

				if(wp->connectionToMe)
					eaPush(&toFG->wpBadConn, wp);
				else if(wp->beacon)
					wp->beacon->pathsBlockedToMe++;
			}

			for(i = 0; i < 20 && nextWp >= 0 && pingpongRev == path->pingpongRev &&
				nextWp < eaSize(&path->waypoints); i++)
			{
				NavPathWaypoint* wp = path->waypoints[nextWp];
				if(wp->gotStuck || wp->dontShortcut)
				{
					dontExpire = true;
					break;
				}
				navPathGetNextWaypoint(path, &nextWp, &pingpongRev);
			}

			if(!dontExpire)
			{
				NavPathWaypoint* expireWp = aiMovementGetNextShortcutWp(path);
				aiMovementExpireToWp(msg, bg, toFG, expireWp, "requested path refine");
			}

			if(path->curWaypoint >= 0 && path->curWaypoint < eaSize(&path->waypoints))
			{
				NavPathWaypoint* wp = path->waypoints[path->curWaypoint];
				if(!wp->gotStuck)
					wp->gotStuck = 1;
				else if(!gConf.bDisableSuperJump && !wp->jumped && !bg->targetUnreachable)
				{
					Vec3 faceDir;

					wp->jumped = true;

					AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 2,
										"Jumping to get unstuck from " LOC_PRINTF_STR " to " LOC_PRINTF_STR,
										vecParamsXYZ(curPos), vecParamsXYZ(wp->pos));

					mrmTargetSetStartJumpBG(msg, wp->pos, 1);

					subVec3(wp->pos, curPos, faceDir);
					if(!vec3IsZeroXZ(faceDir))
					{
						unitQuat(bg->faceRot);
						yawQuat(-getVec3Yaw(faceDir), bg->faceRot);
					}

					aiMovementSetTargetPosBG(msg, bg, wp->pos, 0);
					return true;
				}
				else if(!(disableTeleport || bg->config.teleportDisabled || bg->teleportDisabled) && 
						!bg->targetUnreachable)
				{
					AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 2,
										"Teleporting to get unstuck from " LOC_PRINTF_STR " to " LOC_PRINTF_STR,
										vecParamsXYZ(curPos), vecParamsXYZ(wp->pos));

					bg->doTeleport = true;
					copyVec3(wp->pos, bg->jumpPos);
					return true;
				}
			}

			aiMovementRefineToNextWp(msg, bg, toFG);
			bg->stuckCounter = -20;
		}
	}
	else
	{
		bg->stuckCounter = 0;
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int aiMovementUpdateSplineTarget(const MovementRequesterMsg *msg, 
														 AIMovementBG *bg, 
														 AIMovementToFG *toFG, 
														 NavPath* path,
														 const Vec3 curPos, 
														 U32 isYCapable)
{
	F32 distToTarget = aiMovementGetTargetDistBG(msg, bg, bg->splineTarget, isYCapable, false);
	F32 distToSource = aiMovementGetTargetDistBG(msg, bg, bg->splineSource, isYCapable, false);
	Vec3 dir;
	F32 distToNext;
	int pingpongRev = path->pingpongRev;
	int nextwpindex = path->curWaypoint;
	NavPathWaypoint *targetWp = eaGet(&path->waypoints, path->curWaypoint);

	if(bg->splining && !targetWp)
	{
		bg->recentMinDistToTarget = 0;
		bg->splining = 0;
	}

	if(!bg->splining)
		return false;

	if(bg->stuckCounter > 3)
	{
		AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 5,
							"Finished splining to to (%.2f %.2f %.2f) due to getting stuck",
							vecParamsXYZ(bg->splineTarget));
		
		bg->splining = 0;
		setVec3same(bg->splineSource, 0);
		setVec3same(bg->splineTarget, 0);
		mrmEnableMsgUpdatedToFG(msg);
		copyVec3(bg->splineTarget, toFG->splineTarget);
	}
	else if(distToSource < 2.f*bg->config.distBeforeWaypointToSpline)
	{
		Vec3 tmpPos;
		bool bRaycastTime = false; 
		WorldColl* wc;

		// Find out next waypoint
		copyVec3(bg->splineTarget, tmpPos);

		subVec3(targetWp->pos, bg->splineTarget, dir);
		distToNext = normalVec3(dir);
		if(distToNext > bg->config.distBeforeWaypointToSpline)
		{
			F32 dist = bg->config.distBeforeWaypointToSpline - distToTarget;
			scaleAddVec3(dir, dist, tmpPos, tmpPos);
		}
		else
		{
			AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 5,
								"Finished splining to to (%.2f %.2f %.2f) since spline target is farther than wp",
								vecParamsXYZ(bg->splineTarget));
								
			copyVec3(targetWp->pos, tmpPos);
			bg->splining = 0;
			setVec3same(bg->splineSource, 0);
			setVec3same(bg->splineTarget, 0);
			mrmEnableMsgUpdatedToFG(msg);
			copyVec3(bg->splineTarget, toFG->splineTarget);
			return false;
		}

		bRaycastTime = ABS_TIME_SINCE(bg->timeLastSpline) > SEC_TO_ABS_TIME(0.5);

		if(	!bRaycastTime ||
			!mrmGetWorldCollBG(msg, &wc) ||
			!aiCollideRayWorldColl(wc, NULL, curPos, NULL, tmpPos, STD_AICOLLIDERAY_FLAGS(bg->flying, false)))
		{
			if (bRaycastTime)
				bg->timeLastSpline = ABS_TIME;

			//MM_CHECK_DYNPOS_DEVONLY(tmpPos);
			copyVec3(tmpPos, bg->splineTarget);
			mrmEnableMsgUpdatedToFG(msg);
			copyVec3(bg->splineTarget, toFG->splineTarget);
		}
	}
	else
	{
		AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 5,
							"Finished splining to to (%.2f %.2f %.2f)",
							vecParamsXYZ(bg->splineTarget));
		
		bg->splining = 0;
		setVec3same(bg->splineSource, 0);
		setVec3same(bg->splineTarget, 0);
		mrmEnableMsgUpdatedToFG(msg);
		copyVec3(bg->splineTarget, toFG->splineTarget);
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void _doShortcutCheck(const MovementRequesterMsg *msg, 
										  AIMovementBG *bg, 
										  AIMovementToFG *toFG, 
										  NavPath* path,
										  const Vec3 curPos, 
										  const Vec3 targetPos)
{
	bg->timeLastShortcutCheck = ABS_TIME;

	if(path->curWaypoint >= 0 && path->curWaypoint < eaSize(&path->waypoints) &&
		!path->waypoints[path->curWaypoint]->targetWp)
	{
		int requestedRefine = false;
		F32 remainingPathDist;
		F32 curDistToTarget = distance3(curPos, targetPos);
		U32 pingpongRev = path->pingpongRev;
		int shortcutting = false;
		int nextWp;
		NavPathWaypoint* bestShortcutWp = NULL;
		NavPathWaypoint* shortcutStartWp;
		Vec3 shortcutPos;
		F32 shortcutPathDist;

		// Check if you can shortcut to your target
		remainingPathDist = distance3(curPos, path->waypoints[path->curWaypoint]->pos);

		nextWp = path->curWaypoint;
		if (!navPathGetNextWaypoint(path, &nextWp, &pingpongRev))
		{	// make sure we are not restarting or going off the end of the path
			S32 i, j;
			
			i = path->curWaypoint;
			j = nextWp;

			while(remainingPathDist < 100.f && 
				j >= 0 && j < eaSize(&path->waypoints))
			{
				remainingPathDist += distance3(path->waypoints[i]->pos, path->waypoints[j]->pos);
				i = j;

				if (navPathGetNextWaypoint(path, &j, &pingpongRev))
					break; // we reached the end of the path
			}

			if(bg->orders.movementType!=AI_MOVEMENT_ORDER_NONE && 
				curDistToTarget < 100.f && 
				curDistToTarget < remainingPathDist)
			{
				Vec3 shortcutCheckPos;

				copyVec3(curPos, shortcutCheckPos);
				shortcutCheckPos[1] += 2;
				if(aiMovementShortcutCheckBG(msg, bg, shortcutCheckPos, targetPos, false))
				{
					shortcutting = true;
					AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 3, "Shortcutting to my target");
					bg->recentMinDistToTarget = 0;
					while(path->curWaypoint < eaSize(&path->waypoints) && path->curWaypoint >= 0)
					{
						AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 5,
							"Expiring waypoint at (%.2f, %.2f, %.2f) to shortcut "
							"to my target",
							vecParamsXYZ(path->waypoints[path->curWaypoint]->pos));
						aiMovementReachedWaypoint(msg, path,
							path->waypoints[path->curWaypoint], bg, toFG, false);
					}
				}
			}
		}
		

		if(!shortcutting)
		{
			S32 i, j;
			S32 endOfPath = 0;

			// if you can't shortcut to your target, see if you can shortcut to
			// one of your later waypoints (mostly useful in the case of refining
			// a path suboptimally
			i = path->curWaypoint;
			j = nextWp;
			pingpongRev = path->pingpongRev;
			shortcutStartWp = path->waypoints[path->curWaypoint];

			copyVec3(curPos, shortcutPos);
			shortcutPos[1] += 2;

			shortcutPathDist = remainingPathDist = distance3(curPos, path->waypoints[path->curWaypoint]->pos);

			while(!shortcutting && remainingPathDist < 100 &&
				j < path->curWaypoint + 4 && j > path->curWaypoint - 4 &&
				pingpongRev == path->pingpongRev &&
				!endOfPath &&
				j >= 0 && j < eaSize(&path->waypoints) &&
				(path->waypoints[i]->connectType != NAVPATH_CONNECT_ATTEMPT_SHORTCUT || !bestShortcutWp))
			{
				F32 shortcutDistSQR = distance3Squared(shortcutPos, path->waypoints[j]->pos);
				F32 pathLegDist = distance3(path->waypoints[i]->pos, path->waypoints[j]->pos);
				remainingPathDist += pathLegDist;
				shortcutPathDist += pathLegDist;

				if(path->waypoints[i]->connectType == NAVPATH_CONNECT_ATTEMPT_SHORTCUT)
				{
					// shortcutStartWp is the first waypoint that gets skipped,
					// not the last waypoint you have to get to...
					shortcutStartWp = path->waypoints[j];
					copyVec3(path->waypoints[i]->pos, shortcutPos);
					shortcutPathDist = pathLegDist;
				}
				else if(shortcutDistSQR < SQR(shortcutPathDist) && !path->waypoints[j]->dontShortcut)
				{
					// can shortcut if we're flying and it's a flying connection, 
					// or if we're not flying and have a ground connection
					bool bCanShortcut = (bg->flying) ? path->waypoints[j]->connectType == NAVPATH_CONNECT_FLY :
						path->waypoints[j]->connectType == NAVPATH_CONNECT_GROUND;

					if (bCanShortcut && path->waypoints[j]->connectType != NAVPATH_CONNECT_JUMP)
					{
						if(aiMovementShortcutCheckBG(msg, bg, shortcutPos, path->waypoints[j]->pos, path->waypoints[j]->avoiding))
						{
							AI_DEBUG_PRINT_BG(
								AI_LOG_MOVEMENT, 3, 
								"It seems like I can shortcut from "
								LOC_PRINTF_STR 
								" to " 
								LOC_PRINTF_STR 
								", setting bestShortcutWp.",
								vecParamsXYZ(shortcutPos), 
								vecParamsXYZ(path->waypoints[j]->pos));

							bestShortcutWp = path->waypoints[j];
						}
					}

				}

				i = j;
				endOfPath = navPathGetNextWaypoint(path, &j, &pingpongRev);
			}

			if(bestShortcutWp)
			{
				NavPathWaypoint* oldCurWp = NULL;
				int oldPingPongDir = path->pingpongRev;

				// you shouldn't attempt to shortcut to this waypoint again
				// because you already did (probably didn't actually make it)
				bestShortcutWp->dontShortcut = 1;

				i = path->curWaypoint;
				pingpongRev = path->pingpongRev;

				if(shortcutStartWp != path->waypoints[path->curWaypoint])
				{
					oldCurWp = path->waypoints[path->curWaypoint];
					while(path->waypoints[i] != shortcutStartWp)
						navPathGetNextWaypoint(path, &i, &pingpongRev);

					path->curWaypoint = i;
				}

				while(path->waypoints[path->curWaypoint] != bestShortcutWp)
				{
					AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 5,
						"Expiring waypoint at (%.2f, %.2f, %.2f) to shortcut "
						"to one of my later waypoints",
						vecParamsXYZ(path->waypoints[path->curWaypoint]->pos));
					aiMovementReachedWaypoint(msg, path,
						path->waypoints[path->curWaypoint], bg, toFG, false);
				}

				if(oldCurWp)
				{
					path->curWaypoint = eaFind(&path->waypoints, oldCurWp);
					path->pingpongRev = oldPingPongDir;
				}

				bg->timeLastShortcutCheck = 0;
			}
		}
	}
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static void _doRefineCheck(const MovementRequesterMsg *msg, 
										AIMovementBG *bg, 
										AIMovementToFG *toFG, 
										NavPath* path,
										const Vec3 curPos)
{
	S32 i;
	int requestedRefine = false;

	F32 remainingPathDist = 100 - distance3(curPos, path->waypoints[path->curWaypoint]->pos);

	int pingpongRev = path->pingpongRev;
	int j = path->curWaypoint;

	i = path->curWaypoint;

	bg->timeLastRefineCheck = ABS_TIME;

	if (path->waypoints[path->curWaypoint]->connectType == NAVPATH_CONNECT_ATTEMPT_SHORTCUT &&
		!path->waypoints[path->curWaypoint]->requestedRefine &&
		!bg->refineRequested)
	{
		Vec3 shortcutCheckPos;

		copyVec3(curPos, shortcutCheckPos);
		shortcutCheckPos[1] += 2;
		requestedRefine = aiMovementCheckShortcutWaypoint(msg, bg, toFG, shortcutCheckPos, path->waypoints[path->curWaypoint], pingpongRev);
		bg->refineRequested |= requestedRefine;
	}

	if (!navPathGetNextWaypoint(path, &j, &pingpongRev))
	{
		while(!requestedRefine && remainingPathDist > 0.f && 
				(j >= 0 && j < eaSize(&path->waypoints)) )
		{
			if(path->waypoints[j]->connectType == NAVPATH_CONNECT_ATTEMPT_SHORTCUT &&
				!path->waypoints[j]->requestedRefine)
			{
				requestedRefine = aiMovementCheckShortcutWaypoint(msg, bg, toFG, path->waypoints[i]->pos, path->waypoints[j], pingpongRev);
			}

			remainingPathDist -= distance3(path->waypoints[i]->pos, path->waypoints[j]->pos);
			i = j;
			if (navPathGetNextWaypoint(path, &j, &pingpongRev))
				break;
		}
	}
	
}


// ------------------------------------------------------------------------------------------------------------------
__forceinline static void _checkDistToTarget(const MovementRequesterMsg *msg, 
											 AIMovementBG *bg, 
											 AIMovementToFG *toFG, 
											 NavPath* path,
											 const Vec3 curPos,
											 const Vec3 targetPos,
											 U32 isYCapable)
{
	// check target's distance from end of path vs remaining path len
	if(!bg->config.collisionlessMovement &&
		!bg->refineRequested && bg->orders.movementType!=AI_MOVEMENT_ORDER_NONE &&
		path->curWaypoint >= 0 && path->curWaypoint < eaSize(&path->waypoints) &&
		(!bg->timeLastTargetDistCheck || ABS_TIME_PASSED(bg->timeLastTargetDistCheck, 5)))
	{
		int j, i;
		F32 remainingPathDist;
		int nextWp;
		int count = 0;
		F32 pathEndToTargetDist;
		int pingpongRev = path->pingpongRev;
		int passedDistCheck = 1;

		bg->timeLastTargetDistCheck = ABS_TIME;

		if(bg->usePosLastTargetDistCheck)
		{
			F32 targetPosCheckDiffSQR;
			if(isYCapable)
				targetPosCheckDiffSQR = distance3Squared(targetPos, bg->posLastTargetDistCheck);
			else
				targetPosCheckDiffSQR = distance3SquaredXZ(targetPos, bg->posLastTargetDistCheck);

			if(targetPosCheckDiffSQR<SQR(50))
				passedDistCheck = 0;
		}

		if(passedDistCheck)
		{
			pathEndToTargetDist = distance3(path->waypoints[eaSize(&path->waypoints)-1]->pos, targetPos);
			if(pathEndToTargetDist>75)
			{
				// Check if you can shortcut to your target
				remainingPathDist = distance3(curPos, path->waypoints[path->curWaypoint]->pos);

				nextWp = path->curWaypoint;
				navPathGetNextWaypoint(path, &nextWp, &pingpongRev);

				i = path->curWaypoint;
				j = nextWp;

				// this shouldn't ever be circular because it has a target?
				while(count < 100 && j >= 0 && j < eaSize(&path->waypoints))
				{
					remainingPathDist += distance3(path->waypoints[i]->pos, path->waypoints[j]->pos);
					i = j;
					navPathGetNextWaypoint(path, &j, &pingpongRev);
					count++;
				}
				//devassert(count < 100);

				AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 7, "Remaining path dist: %.2f, pathEndToTarget: %.2f", remainingPathDist, pathEndToTargetDist);
				if(pathEndToTargetDist > remainingPathDist)
				{
					aiMovementRequestRefine(msg, bg, toFG, NULL, bg->targetPos, NULL, path->pingpongRev, 1, 1);
				}
			}
			bg->usePosLastTargetDistCheck = 1;
			copyVec3(targetPos, bg->posLastTargetDistCheck);
		}				
	}
}

// ------------------------------------------------------------------------------------------------------------------
static bool aiMovementCheckFlying(const MovementRequesterMsg *msg, AIMovementBG *bg, AIMovementToFG *toFG)
{
	NavPathWaypoint* wp = eaGet(&bg->path.waypoints, bg->path.curWaypoint);
	if(!bg->alwaysFlying)  // Flight from innate or class, not mutable
	{
		int targetOnGround = false;

		if(bg->config.neverFly)
			bg->shouldFly = 0;
		else if(bg->config.alwaysFly)
			bg->shouldFly = 1;
		else if(wp)
		{
			if(wp->connectType == NAVPATH_CONNECT_FLY)
				bg->shouldFly = 1;
			else if(wp->connectType == NAVPATH_CONNECT_ATTEMPT_SHORTCUT)
				bg->shouldFly = bg->flying;
			else if(wp->connectType != NAVPATH_CONNECT_FLY && bg->flying)
				bg->shouldFly = 0;
			else
				bg->shouldFly = 0;
		}

		if(bg->flying!=bg->shouldFly)
		{
			mrmEnableMsgUpdatedToFG(msg);
			if(bg->shouldFly)
				toFG->turnOnFlight = 1;
			else
				toFG->turnOffFlight = 1;

			return true; //Wait for our flying to be "correct"
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int aiMovementDoMovementChecks(const MovementRequesterMsg *msg, 
	  												 AIMovementBG *bg,
													 AIMovementToFG *toFG,
													 const Vec3 curPos,
													 const Vec3 targetPos,
													 U32 isYCapable,
													 S32 *avoidingTarget)
{
	S32 i;
	int disableShortcutting = globalDisableShortcutting;
	NavPath* path = &bg->path;
	
	if (aiMovementCheckAvoids(msg, bg, toFG, path, curPos, avoidingTarget))
	{
		return true;
	}

	if(aiMovementCheckFlying(msg, bg, toFG))
	{
		return true;
	}

	if (_checkReachable(msg, bg, toFG, path, curPos, isYCapable))
	{
		return true;
	}

	if(!bg->skipStuckDetection)
	{
		if (aiMovementDoStuckDetection(msg, bg, toFG, path, curPos))
		{
			return true;
		}
	}

	bg->skipStuckDetection = 0;

	// If you're doing collisionlessMovement you're either on a path which is telling you exactly
	// where to go, or you didn't make a path in the first place
	disableShortcutting |= !!bg->config.collisionlessMovement;

	if (!disableShortcutting)
	{
		for(i = eaSize(&path->waypoints)-1; !disableShortcutting && i >= 0; i--)
		{
			disableShortcutting |= !!path->waypoints[i]->gotStuck;
			disableShortcutting |= !!path->waypoints[i]->dontShortcut;
		}
	}
	

	if(!disableShortcutting && ABS_TIME_SINCE(bg->timeLastShortcutCheck) > SEC_TO_ABS_TIME(2))
	{
		_doShortcutCheck(msg, bg, toFG, path, curPos, targetPos);
	}

	if(	!bg->config.collisionlessMovement &&
		ABS_TIME_SINCE(bg->timeLastRefineCheck) > SEC_TO_ABS_TIME(0.5) &&
		path->curWaypoint >= 0 &&
		path->curWaypoint < eaSize(&path->waypoints))
	{
		_doRefineCheck(msg, bg, toFG, path, curPos);
	}

	// check target's distance from end of path vs remaining path len
	if(!bg->config.collisionlessMovement &&
		!bg->refineRequested && bg->orders.movementType!=AI_MOVEMENT_ORDER_NONE &&
		eaGet(&path->waypoints, path->curWaypoint) &&
		(!bg->timeLastTargetDistCheck || ABS_TIME_PASSED(bg->timeLastTargetDistCheck, 5)))
	{
		_checkDistToTarget(msg, bg, toFG, path, curPos, targetPos, isYCapable);
	}


	// check angle between path direction and dir to target

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int aiMovementShouldDoPseudoSplineToWaypoint(AIMovementBG *bg, 
														 NavPath* path, 
														 NavPathWaypoint* wp, 
														 const Vec3 curPos,
														 F32 distToTarget)
{
	if(distToTarget > bg->config.distBeforeWaypointToSpline)
		return false; 
	if (ABS_TIME_SINCE(bg->timeLastSpline) < SEC_TO_ABS_TIME(0.5))
		return false;

	{
		int nextwpindex = path->curWaypoint;
		U32 pingpongRev = path->pingpongRev;

		// Find out next waypoint
		navPathGetNextWaypoint(path, &nextwpindex, &pingpongRev);

		if(	pingpongRev == path->pingpongRev && 
			nextwpindex >= 0 && 
			nextwpindex < eaSize(&path->waypoints))
		{
			Vec3 dirnext, dir;
			F32 dot;
			NavPathWaypoint* nextWp = path->waypoints[nextwpindex];

			subVec3(wp->pos, curPos, dir);
			subVec3(nextWp->pos, wp->pos, dirnext);

			normalVec3(dir);
			normalVec3(dirnext);

			dot = dotVec3(dir, dirnext);
			MINMAX1(dot, -1.f, 1.f);  // Limit for safety
			dot = acosf(dot);
			return fabs(dot) > RAD(17.f);
		}
	}

	return false;
}

__forceinline static F32 aimCalcSpeedCheat(F32 distToTarget, F32 myMaxSpeed, F32 targetMaxSpeed)
{
	F32 interp;
	F32 myMaxCheatSpeed = myMaxSpeed * 1.5;
	F32 myTargetCheatSpeed = targetMaxSpeed * 1.5;

	MAX1(myMaxCheatSpeed, myTargetCheatSpeed);

	interp = calcInterpParam(distToTarget, 2.0, 15);
	MINMAX1(interp, 0, 1);

	return myMaxSpeed * (1 - interp) + myMaxCheatSpeed * interp;
}

// ------------------------------------------------------------------------------------------------------------------
__forceinline static int aiMovementProcessWaypoints(const MovementRequesterMsg *msg, 
													   AIMovementBG *bg,
													   AIMovementToFG *toFG,
													   NavPath* path,
													   const Vec3 curPos,
													   S32 onGround,
													   U32 isYCapable,
													   Vec3 targetPosOut,
													   S32 *jumping)
{
	NavPathWaypoint *wp = eaGet(&path->waypoints, path->curWaypoint);

	if(wp)
	{
		Vec3 wpPos;
		
		F32 distToTarget;
		int justJump = false;
		F32 distThreshold = aiMovementGetDistThreshold(msg, bg);
		F32 maxSpeed = 0;
		F32 targetCurrentSpeed = 0;

		copyVec3(wp->pos, wpPos);
		wpPos[1] -= 2;
		distToTarget = aiMovementGetTargetDistBG(msg, bg, wpPos, isYCapable, false);

		mrmGetMaxSpeedBG(msg, &maxSpeed);
		if(bg->config.overrideMovementSpeed)
			maxSpeed = bg->config.overrideMovementSpeed;

		if (!wp->connectionToMe) {	// First waypoint
			if (vecY(path->waypoints[path->curWaypoint]->pos) - vecY(curPos) > 1.5) {  // 1.5 = step-up height, so jump // figure out where the heck else 1.5 is hard coded and fix this
				justJump = true;
			}
		}

		if (bg->orders.movementType == AI_MOVEMENT_ORDER_ENT && 
			lengthVec3(bg->targetVelAvg) > 1)
		{
			distThreshold = 0.1;
		}

		if(!globalAIDisableAirMove || onGround || isYCapable)
		{
			S32 isFollowOffset =	bg->orders.movementType == AI_MOVEMENT_ORDER_ENT && 
									bg->orders.entDetail == AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET;

			if(wp->connectType == NAVPATH_CONNECT_JUMP && (onGround || isYCapable))
			{
				if(!wp->attempted)
				{
					AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 2,
										"Jumping from " LOC_PRINTF_STR " to " LOC_PRINTF_STR " because of a jump connection",
										vecParamsXYZ(curPos), vecParamsXYZ(wp->pos));

					mrmTargetSetStartJumpBG(msg, wp->pos, 1);
					*jumping = true;
					bg->timeLastJump = ABS_TIME;
					wp->attempted = 1;
				}
				else if (bg->stuckCounter > 3)
				{
					AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 2,
										"Teleporting from " LOC_PRINTF_STR " to " LOC_PRINTF_STR " because a jump connection failed",
										vecParamsXYZ(curPos), vecParamsXYZ(wp->pos));

					bg->doTeleport = true;
					copyVec3(wp->pos, bg->jumpPos);
					return true;
				}
			}

			if(onGround && !wp->connectionToMe && !bg->flying && !wp->attemptedShortcut && distToTarget < 7.f && !gConf.bDisableSuperJump)
			{
				WorldColl* wc;

				// if we are on the ground and this waypoint has no connectionToMe 
				//		(meaning it is usually the first or last waypoint)
				// and we are not flying and are close enough to the target

				// only check the aiCollideRay once in these cases.
				wp->attemptedShortcut = true;
						
				if(	mrmGetWorldCollBG(msg, &wc) &&
					aiCollideRayWorldColl(wc, NULL, curPos, NULL, wp->pos, AICollideRayFlag_DOWALKCHECK|AICollideRayFlag_SKIPRAYCAST))
				{
					// there is some walking impedement to this waypoint position, 
					// attempt to jump to the position
					mrmTargetSetStartJumpBG(msg, wp->pos, 1);
					aiMovementSetTargetPosBG(msg, bg, wp->pos, 0);
					*jumping = true;
					bg->timeLastJump = ABS_TIME;
					wp->jumped = true;
					return true;
				}
			}

			// if you're stuck trying to get to a freshly pathfound beacon
			// you might need to jump to get unstuck
			if(wp->beacon && !wp->connectionToMe && bg->stuckCounter > 6 &&
				vecY(wp->pos) > vecY(curPos) + 1 && distToTarget < 10 && // the +1 here looks like the 1.5 above.. might need to clean this up
				!isYCapable)
			{
				if(!wp->jumped && !gConf.bDisableSuperJump)
				{
					AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 2,
										"Jumping to get to my first waypoint in my path (from " LOC_PRINTF_STR " to " LOC_PRINTF_STR,
										vecParamsXYZ(curPos), vecParamsXYZ(wp->pos));

					mrmTargetSetStartJumpBG(msg, wp->pos, 1);
					*jumping = true;
					wp->jumped = true;
					bg->timeLastJump = ABS_TIME;
					bg->stuckCounter -= 20;
				}
				else
				{
					AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 2,
										"Teleporting to get to my first waypoint in my path (from " LOC_PRINTF_STR " to " LOC_PRINTF_STR,
										vecParamsXYZ(curPos), vecParamsXYZ(wp->pos));

					bg->doTeleport = true;
					copyVec3(wp->pos, bg->jumpPos);
					return true;
				}
			}

			// don't try to look at this waypoint when you're close to it.  This is a temporary fix until Adam gets a chance
			// to look at why AI are moving past their waypoints.  [RMARR - 1/4/11]
			if (distToTarget > 0.3f)
			{
				// Set as rotation to avoid spinning in the air when jumping past something
				aiMovementTargetPosToRot(bg, curPos, wp->pos, bg->faceRot);
			}

			if (bg->splining)
			{
				aiMovementSetTargetPosBG(msg, bg, bg->splineTarget, 0);
			}
			else
			{	
				aiMovementSetTargetPosBG(msg, bg, wp->pos, 0);
			}

			bg->overrideSpeed = 0.f;
			if(bg->config.overrideMovementSpeed)
			{
				bg->overrideSpeed = bg->config.overrideMovementSpeed;
				if(isFollowOffset)
				{
					mrmGetEntityCurrentSpeedBG(msg, bg->orders.targetRef, &targetCurrentSpeed);

					bg->overrideSpeed = aimCalcSpeedCheat(distToTarget, bg->overrideSpeed, targetCurrentSpeed);
				}
			}
			else if((bg->metaorders.distRun && bg->metaorders.distWalk) || bg->metaorders.speedCheat)
			{
				F32 targetDistSQR;
				int cheatingAllowed = bg->orders.movementType == AI_MOVEMENT_ORDER_ENT;

				if(bg->orders.movementType==AI_MOVEMENT_ORDER_ENT)
				{
					targetDistSQR = distance3Squared(curPos, bg->targetPos);
				}
				else
				{
					targetDistSQR = distance3Squared(curPos, bg->orders.targetPos);
				}

				if(bg->metaorders.distRun && bg->metaorders.distWalk)
				{
					F32 interpParam = calcInterpParam(	targetDistSQR, 
														SQR(bg->metaorders.distWalk), 
														SQR(bg->metaorders.distRun));
					if(interpParam <= 0.f)
						bg->overrideSpeed = maxSpeed * 0.3f;
					else if(interpParam <= 1.f)
						bg->overrideSpeed = maxSpeed * (0.3f + 0.7f * interpParam);
					else if(bg->metaorders.speedCheat && cheatingAllowed)
						bg->overrideSpeed = maxSpeed * 1.5f;
				}
				else if(targetDistSQR > SQR(20.f) && cheatingAllowed)
				{
					bg->overrideSpeed = maxSpeed*1.5f;
				}
			}
			else if(isFollowOffset)
			{
				mrmGetEntityCurrentSpeedBG(msg, bg->orders.targetRef, &targetCurrentSpeed);
				bg->overrideSpeed = aimCalcSpeedCheat(distToTarget, maxSpeed, targetCurrentSpeed);
			}
			
			if(!bg->runningDoor && wp->connectType==NAVPATH_CONNECT_ENTERABLE)
			{
				// Send the door request back to FG.

				mrmEnableMsgUpdatedToFG(msg);

				if(!toFG->queuedDoor){
					toFG->queuedDoor = StructAlloc(parse_AIMovementQueuedDoor);
				}

				copyVec3(wp->pos, toFG->queuedDoor->pos);

				bg->runningDoor = 1;
			}

			return true;
		}
	}

	return false;
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMovementCheckCompletedOrUpdateTarget(	const MovementRequesterMsg *msg, 
													AIMovementBG *bg,
													AIMovementToFG *toFG,
													NavPath* path,
													const Vec3 curPos,
													const Vec3 targetPos,
													S32 onGround,
													U32 isYCapable,
													S32 jumping, 
													U32 avoidingTarget,
													F32 fCloseEnoughDist)
{
	S32 hasWaypoints = eaSize(&path->waypoints) > 0;
	// clear previous tick's toFG info that we're going to replace (this should
	// really be only done before a sync)
	mrmEnableMsgUpdatedToFG(msg);
	
	toFG->wpDebugCurWp = path->curWaypoint;

	if(path->curWaypoint < 0 || path->curWaypoint >= eaSize(&path->waypoints) ||
		eaSize(&path->waypoints) == 1 && path->waypoints[0]->targetWp ||
		avoidingTarget)
	{
		NavPathWaypoint *wp = eaGet(&path->waypoints, path->curWaypoint);
		int movementCompleted = false;

		if(avoidingTarget)
			movementCompleted = true;
		else if(bg->orders.movementType==AI_MOVEMENT_ORDER_NONE)
			movementCompleted = true;
		else
		{
			Vec3 tmp;
			F32 distToMyTarget = 0;

			copyVec3(targetPos, tmp);
			tmp[1] -= 2;
			distToMyTarget = aiMovementGetTargetDistBG(msg, bg, tmp, isYCapable, true);

			if(distToMyTarget <= fCloseEnoughDist)
			{
				movementCompleted = true;
			}
			else
			{
				if(bg->orders.targetRef && bg->recentMinDistToTarget)
				{
					F32 diff = distance3(curPos, targetPos) - distance3(curPos, bg->targetPos);
					bg->recentMinDistToTarget += diff;
				}

#ifdef AI_MOVEMENTTARGET_PARANOID
				devassert(targetPos[0] > -10000 && targetPos[0] < 10000);
#endif
			}
		}

		if(movementCompleted)
		{
			if(bg->config.continuousCombatMovement && !bg->config.noContinousCombatFacing)
			{
				Vec3 facing;
				Quat curRot;
				Vec3 curPyr;

				mrmGetRotationBG(msg, curRot);
				quatToPYR(curRot, curPyr);
				facing[0] = sin(curPyr[1]);
				facing[1] = 0;
				facing[2] = cos(curPyr[1]);
				//quatToMat3_0(curRot, facing);
				scaleVec3(facing, 100, facing);
				addVec3(facing, targetPos, facing);
				bg->recentMinDistToTarget = 0;
				//MM_CHECK_DYNPOS_DEVONLY(facing);
				copyVec3(facing, bg->targetPos);
				aiMovementSetTargetPosBG(msg, bg, facing, 0.2);
				if(eaSize(&path->waypoints) == 1 && path->waypoints[0]->targetWp && !bg->orders.targetRef)
				{
					//MM_CHECK_DYNPOS_DEVONLY(facing);
					copyVec3(facing, bg->orders.targetPos);
					copyVec3(facing, bg->targetPos);
					copyVec3(facing, path->waypoints[0]->pos);
				}
			}
			else
			{
				bg->updateToFG.movementCompleted = true;
				toFG->updateToFG.movementCompleted = true;
				toFG->updateToFG.updated.movementCompleted = true;
				mrmEnableMsgUpdatedToFG(msg);

				AI_DEBUG_PRINT_TAG_BG(AI_LOG_MOVEMENT, 6, AIMLT_MOVCOM, "%s:%d - UpdateToFG: %d", __FILE__, __LINE__, bg->updateToFG.movementCompleted);

				bg->recentMinDistToTarget = 0;
				mrmReleaseDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET);
			}
		}
		else if((!globalAIDisableAirMove || onGround || isYCapable) && !jumping)
		{
			aiMovementTargetPosToRot(bg, curPos, targetPos, bg->faceRot);

			aiMovementSetTargetPosBG(msg, bg, targetPos, 0);
		}
	}
	else
	{
		// find out how to do this only right before a sync?
		if(path->waypoints[path->curWaypoint]->connectType != NAVPATH_CONNECT_ATTEMPT_SHORTCUT)
		{
			mrmEnableMsgUpdatedToFG(msg);
		}

#ifdef AI_MOVEMENTTARGET_PARANOID
		devassert(path->waypoints[path->curWaypoint]->pos[0] > -10000 && path->waypoints[path->curWaypoint]->pos[0] < 10000);
#endif
	}
}

static void aiMovementDetermineReferenceTargetPosBG(const MovementRequesterMsg *msg, AIMovementBG *bg, Vec3 curPos, Vec3 refPosOut)
{
	copyVec3(bg->targetPos, refPosOut);

	if(bg->splining)
		copyVec3(bg->splineTarget, refPosOut);

	if (gConf.bNewAnimationSystem) {
		//movementYOffset should now be part of the target
	} else {
		refPosOut[1] += bg->config.movementYOffset;
	}
}

__forceinline static int aiMovementGetTargetPositionInternal(AIMovementFG *fg, Vec3 targetOut)
{
	switch(fg->orders.movementType)
	{
		xcase AI_MOVEMENT_ORDER_NONE: {
			return 0;
		}
		xcase AI_MOVEMENT_ORDER_POS: {
			copyVec3(fg->orders.targetPos, targetOut);
			return 1;
		}
		xcase AI_MOVEMENT_ORDER_ENT: {
			Entity *target = entFromEntityRefAnyPartition(fg->orders.targetRef);
			if(!target)
				return 0;
			entGetPos(target, targetOut);
			if (fg->orders.useOffset)
			{
				if (fg->orders.offsetRotRelative)
				{
					Vec3 rotOffset;
					Quat rot;
					Vec3 pyFace;
					entGetFacePY(target, pyFace);
					yawQuat(-pyFace[1], rot);
					quatRotateVec3(rot, fg->orders.targetOffset, rotOffset);
					addVec3(targetOut, rotOffset, targetOut);
				}
				else
				{					
					addVec3(targetOut, fg->orders.targetOffset, targetOut);
				}
			}
			return 1;
		}
	}

	return 0;
}

static bool aiMovementCheckAndExpireWaypoints(const MovementRequesterMsg *msg, AIMovementBG *bg, AIMovementToFG *toFG, Vec3 curPos, S32 isYCapable, F32 fCloseEnoughDist)
{
	NavPath *path = &bg->path;
	int expiredWp = 0;
	NavPathWaypoint* wp;
	Vec3 wpPos;
	F32 distToTarget = FLT_MAX;
	Vec3 myPos;
	S32 numWps = eaSize(&path->waypoints);
		
	mrmGetPositionBG(msg, myPos);

	// Expire as many waypoints as we can
	while(wp = eaGet(&path->waypoints, path->curWaypoint))
	{
		bool expireWaypoint = false;
		/* (RP) 8/23/12 - Removing this bit as it is causing some bad stuck check teleporting until Adam or I have time to 
							create a better solution. 
		// Don't expire the follow waypoint since the guy is just going to keep moving
		// and this causes rubberbanding by "move, done, MOVE, done, MOVE, done"
		if(bg->orders.movementType == AI_MOVEMENT_ORDER_ENT && 
			bg->orders.entDetail == AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET &&
			eaSize(&path->waypoints) == 1 &&
			wp->connectType == NAVPATH_CONNECT_ATTEMPT_SHORTCUT)
		{
			break;
		}
		*/

		copyVec3(wp->pos, wpPos);
		wpPos[1] -= 2;
		distToTarget = aiMovementGetTargetDistBG(msg, bg, wpPos, isYCapable, false);

		// check if the waypoint is expired
		if(distToTarget <= fCloseEnoughDist)
			expireWaypoint = true;
		else if (path->curWaypoint != (numWps - 1))
		{	// make sure it's not the last waypoint before we check 
			// if we've tried going to this wp before 
			if (!vec3IsZero(wp->lastDirToPos))
			{
				// check if we've passed it 
				Vec3 dirToWp;

				subVec3(wp->pos, myPos, dirToWp);
				if (distToTarget < 1.f) {
					dirToWp[1] = 0.f;
				}

				if (dotVec3(wp->lastDirToPos, dirToWp) < 0.f) {	
					expireWaypoint = true;
				} else {
					copyVec3(dirToWp, wp->lastDirToPos);
				}
			}
			else
			{
				subVec3(wp->pos, myPos, wp->lastDirToPos);
			}
		}

		if(expireWaypoint)
		{
			// If we're mostly there or nearly there and having trouble, just bypass it - not worth the effort
			if(wp->connectType == NAVPATH_CONNECT_JUMP)
				AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 5, "Expired a jump connection");

			AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 3,
								"Expiring waypoint at (%.2f, %.2f, %.2f) because I reached it (at " LOC_PRINTF_STR ")",
								vecParamsXYZ(path->waypoints[path->curWaypoint]->pos), 
								vecParamsXYZ(curPos));

			expiredWp++;
			// something fishy is happening so break out // like what ? I think we're shooting ourselves in the foot here but need to look into it more
			if(expiredWp > 6)
				break;

			bg->recentMinDistToTarget = 0;
			if (aiMovementReachedWaypoint(msg, path, wp, bg, toFG, true))
				return false;
		}
		else
			break;
	}

	if(wp)
	{
		copyVec3(wp->pos, wpPos);
		wpPos[1] -= 2;
		distToTarget = aiMovementGetTargetDistBG(msg, bg, wpPos, isYCapable, false);

		// Check for going through doors
		if(!bg->runningDoor && wp->connectType==NAVPATH_CONNECT_ENTERABLE)
		{
			// Send the door request back to FG.
			mrmEnableMsgUpdatedToFG(msg);

			if(!toFG->queuedDoor){
				toFG->queuedDoor = StructAlloc(parse_AIMovementQueuedDoor);
			}

			copyVec3(wp->pos, toFG->queuedDoor->pos);

			bg->runningDoor = 1;
		}
		// Check for splining
		else if(aiMovementShouldDoPseudoSplineToWaypoint(bg, path, wp, curPos, distToTarget))
		{
			Vec3 dir;
			F32 distToNext;
			S32 nextwpindex = path->curWaypoint;
			S32 pingpongRev = path->pingpongRev;
			NavPathWaypoint* nextWp;
			WorldColl* wc;

			navPathGetNextWaypoint(path, &nextwpindex, &pingpongRev);
			nextWp = path->waypoints[nextwpindex];

			bg->timeLastSpline = ABS_TIME;

			//MM_CHECK_DYNPOS_DEVONLY(wp->pos);
			copyVec3(wp->pos, bg->splineTarget);

			subVec3(nextWp->pos, wp->pos, dir);
			distToNext = normalVec3(dir);
			if(distToNext > bg->config.distBeforeWaypointToSpline)
			{
				F32 dist = bg->config.distBeforeWaypointToSpline - distToTarget;
				scaleAddVec3(dir, dist, bg->splineTarget, bg->splineTarget);
				//MM_CHECK_DYNPOS_DEVONLY(bg->splineTarget);
			}
			else
			{
				//MM_CHECK_DYNPOS_DEVONLY(nextWp->pos);
				copyVec3(nextWp->pos, bg->splineTarget);
			}

			if(	!mrmGetWorldCollBG(msg, &wc) ||
				!aiCollideRayWorldColl(wc, NULL, curPos, NULL, bg->splineTarget, STD_AICOLLIDERAY_FLAGS(bg->flying, false)))
			{
				copyVec3(wp->pos, bg->splineSource);
				bg->splining = 1;
				aiMovementReachedWaypoint(msg, path, wp, bg, toFG, false);
				mrmEnableMsgUpdatedToFG(msg);
				copyVec3(bg->splineTarget, toFG->splineTarget);

				AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 5,
									"Expiring waypoint at (%.2f, %.2f, %.2f) to spline to (%.2f %.2f %.2f)",
									vecParamsXYZ(path->waypoints[path->curWaypoint]->pos), 
									vecParamsXYZ(bg->splineTarget));
			}
		}
	}

	return false;
}

static void aiMovementHandleRotation(const MovementRequesterMsg *msg, AIMovementBG *bg, Quat faceQuat)
{
	if(bg->rotationHandler)
	{
		bg->rotationHandler->func(msg, bg, faceQuat);
		mrmRotationTargetSetAsRotationBG(msg, bg->rotationHandler->faceOut);
	}
	else
		mrmRotationTargetSetAsRotationBG(msg, faceQuat);
}

// ------------------------------------------------------------------------------------------------------------------
static void aiMovementHandleCreateOutput(const MovementRequesterMsg *msg, AIMovementBG *bg, AIMovementToFG *toFG)
{
	U32 dataClassBit = msg->in.bg.createOutput.dataClassBit;
	S32 onGround = 1;
	Vec3 groundNormal = {0.0f, 1.0f, 0.0f};

	if(toFG->id != bg->id)
	{
		toFG->id = bg->id;
		mrmEnableMsgUpdatedToFG(msg);
	}

	if(!bg->debugForceProcess)
	{
		if(bg->sleeping)
			return;

		if(!mrmGetOnGroundBG(msg, &onGround, groundNormal))
			return;
	}

	if(globalPseudoSplineTest>0)
	{
		bg->config.distBeforeWaypointToSpline = globalPseudoSplineTest;
	}

	// this is just to rerequest pathfinds when our first request gets ignored
	// due to the one pathfind per three seconds limitation
	if(dataClassBit & MDC_BIT_POSITION_TARGET && bg->refineRequested)
	{
		if(++bg->refineRequestWait > 100)
		{
			toFG->refine = bg->refine;
			mrmEnableMsgUpdatedToFG(msg);
			bg->refineRequestWait = 0;
		}
	}

	onGround = onGround && vecY(groundNormal)>0.25 && ABS_TIME_SINCE(bg->timeLastJump) > SEC_TO_ABS_TIME(0.5);

	if(dataClassBit & MDC_BIT_POSITION_CHANGE)
	{
		bg->doTeleport = 0;
		mrmSetPositionBG(msg, bg->jumpPos);
		mrmReleaseDataOwnershipBG(msg, MDC_BIT_POSITION_CHANGE);
	}
	else if(dataClassBit & MDC_BIT_POSITION_TARGET && !bg->doMove)
	{
		bg->doMoveDone = 1;
		bg->updateToFG.movementCompleted = 1;
		toFG->updateToFG.movementCompleted = bg->updateToFG.movementCompleted;
		toFG->updateToFG.updated.movementCompleted = true;
		mrmEnableMsgUpdatedToFG(msg);

		AI_DEBUG_PRINT_TAG_BG(AI_LOG_MOVEMENT, 6, AIMLT_MOVCOM, "%s:%d - UpdateToFG: %d", __FILE__, __LINE__, bg->updateToFG.movementCompleted);

		mrmReleaseDataOwnershipBG(msg, MDC_BIT_POSITION_TARGET);
	}
	else if((dataClassBit & MDC_BIT_POSITION_TARGET && !bg->updateToFG.movementCompleted && !bg->refineRequested) ||
			bg->debugForceProcess)
	{
		Vec3 curPos;
		Vec3 targetPos;
		int jumping = false;
		S32 avoiding = false;
		S32 targetChanged = false;
		F32 fCloseEnoughDist;

		U32 isYCapable = bg->flying || bg->canFly;
		NavPath* path = &bg->path;

		// This needs to be consistent across this function, even when stuckCounter changes, so we don't thrash.
		fCloseEnoughDist = aiMovementGetDistThreshold(msg, bg);

#ifdef AI_MOVEMENTTARGET_PARANOID
		devassert(bg->orders.movementType==AI_MOVEMENT_ORDER_NONE || !vec3IsZero(bg->targetPos));
		devassert(!path->circular || bg->orders.movementType==AI_MOVEMENT_ORDER_NONE);
#endif

		PERFINFO_AUTO_START("1", 1);

		mrmEnableMsgUpdatedToFG(msg);
		
		eaClear(&toFG->wpDebugCurPath);
		eaPushEArray(&toFG->wpDebugCurPath, &path->waypoints);
		
		mrmGetPositionBG(msg, curPos);
		curPos[1] += 2;

		aiMovementCheckAndExpireWaypoints(msg, bg, toFG, curPos, isYCapable, fCloseEnoughDist);

		aiMovementUpdateSplineTarget(msg, bg, toFG, &bg->path, curPos, isYCapable);

		aiMovementDetermineReferenceTargetPosBG(msg, bg, curPos, targetPos);

		if(!globalAIDisableAirMove || onGround || isYCapable)
		{
			if (aiMovementDoMovementChecks(msg, bg, toFG, curPos, targetPos, isYCapable, &avoiding))				
			{
				PERFINFO_AUTO_STOP(); // 1
				return;
			}

			bg->triedMovingLastFrame = false;
		}

		PERFINFO_AUTO_STOP_START("2", 1);

		if (aiMovementProcessWaypoints(msg, bg, toFG, path, curPos, onGround, isYCapable, targetPos, &jumping))
		{
			PERFINFO_AUTO_STOP();// 2
			return;
		}

		PERFINFO_AUTO_STOP_START("3", 1);
		
		if(!globalAIDisableAirMove || onGround || isYCapable)
		{
			aiMovementCheckCompletedOrUpdateTarget(msg, bg, toFG, path, curPos, targetPos, onGround, isYCapable, jumping, avoiding, fCloseEnoughDist);
		}

		PERFINFO_AUTO_STOP();
	}
	else if((dataClassBit & MDC_BIT_POSITION_TARGET && bg->updateToFG.movementCompleted && !bg->refineRequested) ||
		bg->debugForceProcess)
	{
		Vec3 vel;
		mrmGetVelocityBG(msg, vel);
		if(lengthVec3Squared(vel)>0)
			aiMovementSetTargetAsStoppedBG(msg, bg);
	}
	
	if(dataClassBit & MDC_BIT_ROTATION_TARGET)
	{
		if(!bg->doMove || bg->updateToFG.movementCompleted)
		{
			Quat faceQuat = {0};
			switch(bg->rotation.type)
			{
			xcase AI_MOVEMENT_ROTATION_NONE:
				; // no rotating to do
			xcase AI_MOVEMENT_ROTATION_POS: {
				Vec3 curPos;
				mrmGetPositionBG(msg, curPos);
				AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 7,
					"Rotating to final face pos " LOC_PRINTF_STR,
					vecParamsXYZ(bg->rotation.finalFacePos));
				
				aiMovementTargetPosToRot(bg, curPos, bg->rotation.finalFacePos, faceQuat);
			}
			xcase AI_MOVEMENT_ROTATION_ROT:
				AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 7,
					"Rotating to final face rot");
				copyQuat(bg->rotation.finalFaceRot, faceQuat);
			xcase AI_MOVEMENT_ROTATION_ENTREF:
			{
				Vec3 curPos;
				Vec3 targetPos;

				if(!mrmGetEntityPositionBG(msg, bg->rotation.finalFaceEntRef, targetPos))
					break;

				if(!mrmGetPositionBG(msg, curPos))
					devassert(0);
				
				AI_DEBUG_PRINT_BG(AI_LOG_MOVEMENT, 7,
					"Rotating to final face rot (towards entref %d)",
					bg->rotation.finalFaceEntRef);

				aiMovementTargetPosToRot(bg, curPos, targetPos, faceQuat);
			}
			xdefault:
				devassert(0);
			}

			if(bg->rotation.type != AI_MOVEMENT_ROTATION_NONE)
				aiMovementHandleRotation(msg, bg, faceQuat);
		}
		else
		{
			aiMovementHandleRotation(msg, bg, bg->faceRot);
		}

		if(bg->config.overrideMovementTurnRate!=-1)
			mrmRotationTargetSetTurnRateAsOverrideBG(msg, bg->config.overrideMovementTurnRate);
		else
			mrmRotationTargetSetTurnRateAsNormalBG(msg);

		if(bg->updateToFG.movementCompleted || !bg->doMove)
		{
			if(!aiMovementWantRotation(msg, bg))
				mrmReleaseDataOwnershipBG(msg, MDC_BIT_ROTATION_TARGET);
		}
	}

	if(dataClassBit & MDC_BIT_ANIMATION)
	{
		int i;

		for(i=0; i<ea32Size(&bg->animHold); i++)
			mrmAnimAddBitBG(msg, bg->animHold[i]);

		if(bg->animHoldClear)
		{
			bg->animHoldClear = false;
			mrmAnimAddBitBG(msg, mmAnimBitHandles.move);
		}
	}
}

static void aiMovementHandleUpdatedToFG(const MovementRequesterMsg* msg,
										AIMovementFG* fg,
										AIMovementToFG* toFG)
{
	int i, n;
	Entity* e = NULL;
	Entity* eOwner = NULL;
	S32 iPartitionIdx = 0;
	AIVarsBase* aib;

	if(mrmGetManagerUserPointerFG(msg, &e)){
		eOwner = entFromEntityRefAnyPartition(e->erOwner);
		iPartitionIdx = entGetPartitionIdx(e);
	}

	if(toFG->queuedDoor){
		MovementRequester *mr = NULL;
		
		PERFINFO_AUTO_START("queued door", 1);

		mrmRequesterCreateFG(msg,
							&mr,
							"DoorMovement",
							NULL,
							0);

		mrDoorStartWithPosition(mr,
								aiDoorComplete,
								msg->in.fg.mr,
								toFG->queuedDoor->pos,
								1);
										
		StructDestroySafe(parse_AIMovementQueuedDoor, &toFG->queuedDoor);

		PERFINFO_AUTO_STOP();
	}

	aib = SAFE_MEMBER(e, aibase);

	if(aib)
	{
		if(aib->debugCurPath)
		{
			eaClearStruct(&aib->debugCurPath, parse_AIDebugWaypoint);
		}

		if(e->myRef == aiDebugEntRef || aib->debugPath)
		{
			AIDebugWaypoint* startWp = StructAlloc(parse_AIDebugWaypoint);

			entGetPos(e, startWp->pos);
			startWp->type = AI_DEBUG_WP_OTHER;
			startWp->pos[1] += 2.0f;

			eaPush(&aib->debugCurPath, startWp);

			n = eaSize(&toFG->wpDebugCurPath);
			for(i = 0; i < n; i++)
			{
				NavPathWaypoint* navWp = toFG->wpDebugCurPath[i];
				AIDebugWaypoint* wp = StructAlloc(parse_AIDebugWaypoint);

#ifdef AI_MOVEMENTTARGET_PARANOID
				devassert(!vec3IsZero(navWp->pos));
#endif
				copyVec3(navWp->pos, wp->pos);
						
				switch(navWp->connectType)
				{
				xcase NAVPATH_CONNECT_ATTEMPT_SHORTCUT:
					wp->type = AI_DEBUG_WP_SHORTCUT;
				xcase NAVPATH_CONNECT_GROUND:
					wp->type = AI_DEBUG_WP_GROUND;
				xcase NAVPATH_CONNECT_JUMP:
					wp->type = AI_DEBUG_WP_JUMP;
				xdefault:
					wp->type = AI_DEBUG_WP_OTHER;
				}

				eaPush(&aib->debugCurPath, wp);
			}
			eaClear(&toFG->wpDebugCurPath);
			aib->debugCurWp = toFG->wpDebugCurWp;

			aib->timeDebugCurPathUpdated = ABS_TIME_PARTITION(iPartitionIdx);
		}

		if(eaSize(&toFG->logEntries)){
			aiDebugLogFlushEntries(e, aib, &toFG->logEntries);
		}
	}

	for(i=eaSize(&toFG->wpBadConn)-1; i>=0; i--)
		beaconConnectionSetBad(toFG->wpBadConn[i]->connectionToMe);

	for(i=eaSize(&toFG->wpBadConnReset)-1; i>=0; i--)
		beaconConnectionResetBad(toFG->wpBadConnReset[i]->connectionToMe);

	eaClear(&toFG->wpBadConn);
	eaClear(&toFG->wpBadConnReset);

	eaPushEArray(&fg->wpPassed, &toFG->wpPassed);

	FOR_EACH_IN_EARRAY(toFG->wpDestroy, NavPathWaypoint, wp)
	{
		if(globalWPParanoia)
			assert(wp->dts.dts==DTS_TOFG);
		dtsSetState(&wp->dts, DTS_INFG);
	}
	FOR_EACH_END

	eaPushEArray(&fg->wpDestroy, &toFG->wpDestroy);

	eaClear(&toFG->wpPassed);
	eaClear(&toFG->wpDestroy);

	eaClearEx(&toFG->avoidDestroy, aiAvoidEntryRemove);

	if(SAFE_MEMBER(e, aibase) && toFG->updateToFG.updated.movementCompleted)
	{
		aib->currentlyMoving = !toFG->updateToFG.movementCompleted;
		if(!aib->currentlyMoving)
			aib->leavingAvoid = false;

		AI_DEBUG_PRINT_TAG(e, AI_LOG_MOVEMENT, 6, AIMLT_CURMOV, "%s:%d Currently Moving %d", __FILE__, __LINE__, aib->currentlyMoving);

		if(aib->currentlyMoving)
		{
			entSetActive(e);
		}
		else 
		{
			entGetPos(e, aib->ambientPosition);
			if(fg->orders.movementType!=AI_MOVEMENT_ORDER_ENT)
				aiMovementSetMovementType(&fg->orders, AI_MOVEMENT_ORDER_NONE, 0);
		}


		if (e->pPlayer)
		{
			e->pPlayer->bMovingToLocation = aib->currentlyMoving;
			entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
		}
	}
	toFG->distMovedSinceSync = 0;

	if(SAFE_MEMBER(e, aibase) && fg->needsTeleport)
	{
		Vec3 sourcePos;
		// Need a teleport
		Beacon *b = NULL;

		PERFINFO_AUTO_START("needs teleport", 1);

		entGetPos(e, sourcePos);
		b = beaconGetNearestBeacon(iPartitionIdx, sourcePos);
		fg->needsTeleport = 0;
		if(b)
		{
			entSetPos(e, b->pos, 1, "aiMovement->needsTeleport");
		}

		PERFINFO_AUTO_STOP();
	}

	if(aib && (toFG->turnOnFlight || toFG->turnOffFlight))
	{
		PERFINFO_AUTO_START("flight toggle", 1);

		aiMovementFly(e, aib, toFG->turnOnFlight && !toFG->turnOffFlight);

		toFG->turnOnFlight = 0;
		toFG->turnOffFlight = 0;

		PERFINFO_AUTO_STOP();
	}

	copyVec3(toFG->splineTarget, fg->splineTarget);
	setVec3same(toFG->splineTarget, 0);

	ZeroStruct(&toFG->updateToFG.updated);

	devassert(e);

	// have to process passed waypoints before destroy waypoints (wp can be in both)
	n = eaSize(&fg->wpPassed);

	for(i = 0; i < n; i++)
	{
		//printf("%d: execing waypoint: %x\n", e->myRef, fg->wpPassed[i]);
		CommandQueue_ExecuteAllCommandsEx(fg->wpPassed[i]->commandsWhenPassed, true);
	}

	eaClear(&fg->wpPassed);

	n = eaSize(&fg->wpDestroy);

	if(n)
	{
		eaQSort(fg->wpDestroy, ptrCmp);
		for(i = 0; i < n; i++)
		{
			NavPathWaypoint* wp = fg->wpDestroy[i];

			if(globalWPParanoia)
				assert(wp->dts.dts==DTS_INFG);
			dtsSetState(&wp->dts, DTS_FREE);

			destroyNavPathWaypoint(wp);
		}
		eaClear(&fg->wpDestroy);
	}

	n = eaSize(&toFG->animFxDestroy);

	if(n)
	{
		PERFINFO_AUTO_START("anim fx destroy", 1);

		for(i = 0; i < n; i++)
			aiMovementAnimFXHandleDestroy(toFG->animFxDestroy[i]);

		eaSetSize(&toFG->animFxDestroy, 0);

		PERFINFO_AUTO_STOP();
	}

	if(fg->id != toFG->id)
	{
		ZeroStruct(&toFG->refine);
		AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 6, "Mismatched id - toFG (%d) - fg (%d)", toFG->id, fg->id);
		return;
	}

	if (toFG->refine.requestRefine && aib &&
		(	toFG->refine.outOfAvoidArea		|| 
			(eOwner && entIsPlayer(eOwner)) || 
			ABS_TIME_SINCE_PARTITION(iPartitionIdx, aib->time.lastPathFind) > SEC_TO_ABS_TIME(3)))
	{
		Vec3 sourcePos;
		Vec3 targetPos;
		int avoidTarget = false;
		int avoidSource = false;

		PERFINFO_AUTO_START("refine path", 1);

		// Always inform the BG regardless of whether we do a search or it fails
		mrmEnableMsgCreateToBG(msg);
		fg->updated.refine = 1;

		// clear out any result refine data on the fg struct
		fg->refineInsertWp = NULL;
		fg->refineInsertReverse = 0;
		fg->refineAgain = 0;

		// Do this here so even if the refine is invalid, it updates the refineId and clears the path
		navPathClear(&fg->pathRefine);
		if(toFG->refine.pathResetOnRefine)
			fg->refineId = 0;
		else
			fg->refineId = toFG->id;
				
		if(fg->orders.movementType==AI_MOVEMENT_ORDER_ENT && fg->orders.targetRef)
		{
			Entity *ent = entFromEntityRefAnyPartition(fg->orders.targetRef);
			if(ent)
			{
				aiMovementGetTargetPositionInternal(fg, targetPos);

				for(i = eaSize(&aib->avoid.base.volumeEntities) - 1; i >= 0; i--)
				{
					AIVolumeEntry* entry = aib->avoid.base.volumeEntities[i];
					if(aiAvoidEntryCheckPoint(ent, targetPos, entry, true, NULL))
					{
						avoidTarget = true;
						//aiMovementResetPath(e, aib);
					}
					if(aiAvoidEntryCheckPoint(e, NULL, entry, true, NULL))
					{
						avoidSource = true;
					}
				}
			}
		}

		if(!avoidTarget || avoidSource)
		{
			int valid = true;

			if(toFG->refine.useRefineStartPos)
				copyVec3(toFG->refine.refineStartPos, sourcePos);
			else
			{
				entGetPos(e, sourcePos);
				sourcePos[1] += 2;
			}

			if(toFG->refine.outOfAvoidArea)
			{
				if(toFG->refine.useRefineTargetPos)
					copyVec3(toFG->refine.refineTarget, targetPos);
				else
				{
					Beacon* escapeBeacon = aiFindAvoidBeaconInRange(e, aib);
					if(escapeBeacon)
					{
						copyVec3(escapeBeacon->pos, targetPos);
					}
					else
					{
						AI_DEBUG_PRINT_BG(	AI_LOG_MOVEMENT, 2,
											"Refine: outOfAvoidArea could not find a target refine position.");

						if (!vec3IsZero(toFG->refine.refineTarget))
						{
							copyVec3(toFG->refine.refineTarget, targetPos);
						}
						else
						{
							valid = false;
						}
					}
				}
			}
			else
				copyVec3(toFG->refine.refineTarget, targetPos);

			if(valid)
			{
				AIMovementTargetFlags flags = AI_MOVEMENT_TARGET_CRITICAL;
				U32 inAvoid = false;
				NavSearchResultType result = 0;

				aib->time.lastPathFind = ABS_TIME_PARTITION(iPartitionIdx);

				AI_DEBUG_PRINT(	e, AI_LOG_MOVEMENT, 5,
								"About to do requested path refine (from: %s:%d)",
								toFG->refine.caller_fname, toFG->refine.line);

				inAvoid = toFG->refine.outOfAvoidArea;
				if(!inAvoid)
					flags |= AI_MOVEMENT_TARGET_DONT_SHORTCUT;

				if(aiMovementMakePath(e, aib, &fg->pathRefine, sourcePos, targetPos, NULL, NULL, &result, inAvoid, flags))
				{
					AI_DEBUG_PRINT(	e, AI_LOG_MOVEMENT, 4,
									"Succeeded in refine pathfind to target from " LOC_PRINTF_STR " to " LOC_PRINTF_STR,
									vecParamsXYZ(sourcePos), vecParamsXYZ(targetPos));

					fg->refineInsertWp = toFG->refine.refineInsertWp;
					fg->refineInsertReverse = toFG->refine.refineInsertReverse;
					fg->refineAgain = toFG->refine.refineAgain;

					if(toFG->refine.pathResetOnRefine)
					{
						navPathClear(&fg->pathNormal);
						mrmEnableMsgCreateToBG(msg);
						fg->updated.path = 1;
						fg->pathReset = 1;
					}

					if(toFG->refine.outOfAvoidArea)
					{
						NavPathWaypoint *lastwp = NULL;

						fg->doMove = 1;
						mrmEnableMsgCreateToBG(msg);
						fg->updated.followTargetPos = 1;
						for(i = eaSize(&fg->pathRefine.waypoints)-1; i >= 0; i--)
							fg->pathRefine.waypoints[i]->avoiding = true;

						lastwp = eaTail(&fg->pathRefine.waypoints);
						if(lastwp)
						{
							if(!lastwp->commandsWhenPassed)
								lastwp->commandsWhenPassed = CommandQueue_Create(5, false);
							QueuedCommand_aiMovementAvoidLeft(lastwp->commandsWhenPassed, e, aib);
						}
						
						aib->leavingAvoid = true;
					}
				}
				else
				{
					AI_DEBUG_PRINT(	e, AI_LOG_MOVEMENT, 1, "Failed refine pathfind to target from "
									LOC_PRINTF_STR " to " LOC_PRINTF_STR " (Reason: %s)",
									vecParamsXYZ(sourcePos),
									vecParamsXYZ(targetPos),
									StaticDefineIntRevLookup(NavSearchResultTypeEnum, result));

					navPathClear(&fg->pathRefine);
				}

				if(result == NAV_RESULT_NO_SOURCE_BEACON && 
					!(disableTeleport || fg->teleportDisabled || fg->config.teleportDisabled) )
				{
					fg->needsTeleport = 1;
				}
			}
		}

		ZeroStruct(&toFG->refine);

		PERFINFO_AUTO_STOP();
	}
}

// handler for callback message MR_MSG_FG_CREATE_TOBG, sent from EntityMovementManager.  Runs in foreground thread.
// "CreateToBG" means "prepare my foreground data for the background thread".  mrmEnableMsgUpdatedToBG marks the data as needing to be sent.
static void aiMovementHandleCreateToBG(	const MovementRequesterMsg* msg,
										AIMovementFG* fg,
										AIMovementToBG* toBG)
{
	Entity* e;
	AIVarsBase* aib;

	if(!mrmGetManagerUserPointerFG(msg, &e)){
		return;
	}

	aib = SAFE_MEMBER(e, aibase);

	if(fg->updated.debugForceProcess)
	{
		mrmEnableMsgUpdatedToBG(msg);
		toBG->updated.debugForceProcess = 1;
		fg->updated.debugForceProcess = 0;
		toBG->debugForceProcess = fg->debugForceProcess;
	}

	if(fg->debugForceProcess)
	{
		mrmEnableMsgUpdatedToBG(msg);
		toBG->debugForceProcess = 1;
	}

	if(fg->updated.doorComplete){
		mrmEnableMsgUpdatedToBG(msg);
		toBG->updated.doorComplete = 1;
	}
		
	if(fg->updated.sleeping)
	{
		toBG->sleeping = fg->sleeping;
		mrmEnableMsgUpdatedToBG(msg);
		toBG->updated.sleeping = 1;
		AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 3, "New sleep value: %d", fg->sleeping);
	}

	if(fg->updated.animBits)
	{
		toBG->updated.animBits = 1;
		mrmEnableMsgUpdatedToBG(msg);

		eaCopy(&toBG->animFxAdd, &fg->animFxAdd);
		eaSetSize(&fg->animFxAdd, 0);

		eaCopy(&toBG->animFxCancel, &fg->animFxCancel);
		eaSetSize(&fg->animFxCancel, 0);
	}

	if(fg->updated.animHold)
	{
		toBG->updated.animHold = 1;
		ea32Copy(&toBG->animHold, &fg->animHold);

		mrmEnableMsgUpdatedToBG(msg);
	}

	if(fg->updated.flying)
	{
		toBG->flying = fg->flying;
		toBG->updated.flying = 1;
		mrmEnableMsgUpdatedToBG(msg);
	}
#define FLAG_UPDATE(flagName)						\
				if(fg->updated.flagName)			\
				{									\
					toBG->flagName = fg->flagName;	\
					toBG->updated.flagName = 1;		\
					fg->updated.flagName = 0;		\
					mrmEnableMsgUpdatedToBG(msg);	\
				}
	FLAG_UPDATE(teleportDisabled);
	FLAG_UPDATE(rotationDisabled);
#undef FLAG_UPDATE

	if(e)
	{
		// MS: This is violating the CreateToBG->ToBG principle.  It only gets set if
		//     something else happens to update, which is probably fine.
				
		assert(aib && aib->powers);
		toBG->alwaysFlying = aib->powers->alwaysFlight;
		toBG->canFly = aiMovementGetCanFlyEntRef(entGetRef(e));

		if(aiMovementGetNeverFlyEntRef(entGetRef(e)))
		{
			toBG->alwaysFlying = 0;
			toBG->canFly = 0;
		}
	}

	if(disableAIMovement)
		return;

	if(fg->updated.path)
	{
		toBG->updated.path = 1;
		mrmEnableMsgUpdatedToBG(msg);

		fg->id = toBG->id = aiMovementGetNewId();
		toBG->doMove = fg->doMove;

		if(fg->pathReset)
		{
			toBG->pathReset = 1;
			fg->pathReset = 0;
			toBG->curWaypoint = 0;

			toBG->circular = fg->pathNormal.circular;
			toBG->pingpong = fg->pathNormal.pingpong;
			toBG->pingpongRev = fg->pathNormal.pingpongRev;
			toBG->curWaypoint = fg->pathNormal.curWaypoint;

			if(eaSize(&fg->pathNormal.waypoints))
			{
				assert(eaSize(&toBG->waypointsPath)==0);

				FOR_EACH_IN_EARRAY(fg->pathNormal.waypoints, NavPathWaypoint, wp)
				{
					if(globalWPParanoia)
						assert(wp->dts.dts==DTS_INFG);
					dtsSetState(&wp->dts, DTS_TOBG);
				}
				FOR_EACH_END

				eaPushEArray(&toBG->waypointsPath, &fg->pathNormal.waypoints);
				eaClear(&fg->pathNormal.waypoints);

				assert(toBG->curWaypoint >=0 && (int)toBG->curWaypoint < eaSize(&toBG->waypointsPath));
			}
		}
	}

	if(fg->updated.followTargetPos)
	{
		toBG->updated.followTargetPos = 1;

		mrmEnableMsgUpdatedToBG(msg);

		devassertmsg(fg->updated.path || fg->orders.movementType!=AI_MOVEMENT_ORDER_NONE || fg->updated.refine,
			"Why is the pos getting updated when there is no movement target?");
	}

	if(fg->updated.orders)
	{
		toBG->updated.orders = 1;

		toBG->orders = fg->orders;

		toBG->flags = fg->flags;

		mrmEnableMsgUpdatedToBG(msg);

#ifdef AI_MOVEMENTTARGET_PARANOID
		devassert(fg->orders.movementType!=AI_MOVEMENT_ORDER_NONE ||
			!fg->doMove || !vec3IsZero(fg->orders.target));
#endif
	}

	if(fg->updated.metaorders)
	{
		toBG->updated.metaorders = 1;

		toBG->metaorders = fg->metaorders;
		mrmEnableMsgUpdatedToBG(msg);
	}

	if(fg->updated.config)
	{
		toBG->updated.config = 1;
		mrmEnableMsgUpdatedToBG(msg);
		toBG->config = fg->config;
	}

	if(fg->updated.rotation)
	{
		toBG->updated.rotation = 1;

		mrmEnableMsgUpdatedToBG(msg);
		toBG->rotation = fg->rotation;
	}

	if(fg->updated.refine)
	{
		mrmEnableMsgUpdatedToBG(msg);
		toBG->updated.refine = 1;
		if(!fg->refineId || fg->refineId == fg->id)
		{
			toBG->refineInsertWp = fg->refineInsertWp;
			toBG->refineInsertReverse = fg->refineInsertReverse;
			toBG->refineAgain = fg->refineAgain;
			devassert(!eaSize(&toBG->waypointsRefine));
			eaClear(&toBG->waypointsRefine);

			FOR_EACH_IN_EARRAY(fg->pathRefine.waypoints, NavPathWaypoint, wp)
			{
				if(globalWPParanoia)
					assert(wp->dts.dts==DTS_INFG);
				dtsSetState(&wp->dts, DTS_TOBG);
			}
			FOR_EACH_END

			eaPushEArray(&toBG->waypointsRefine, &fg->pathRefine.waypoints);
			eaClear(&fg->pathRefine.waypoints);
		}
		else
			navPathClear(&fg->pathRefine);
	}

	if(fg->updated.avoid)
	{
		int i;
		static AIVolumeEntry **diffAddRemove = NULL;
		static AIVolumeEntry **diffRemoveAdd = NULL;
		static AIVolumeEntry **intAddRemove = NULL;
		toBG->updated.avoid = 1;
		mrmEnableMsgUpdatedToBG(msg);

#ifdef AI_PARANOID_AVOID
		{
			// Check to see if we're adding one that's already queued to go...
			AIVolumeEntry **check = NULL;
			eaDiffAddr(&fg->avoidAdd, &toBG->avoidAdd, &check);
			devassert(eaSize(&fg->avoidAdd)==eaSize(&check));

			eaClear(&check);
			eaDiffAddr(&fg->avoidRemove, &toBG->avoidRemove, &check);
			devassert(eaSize(&fg->avoidRemove)==eaSize(&check));
			eaDestroy(&check);
		}
#endif
		devassert(eaSize(&toBG->avoidAdd)==0);
		devassert(eaSize(&toBG->avoidRemove)==0);
		eaClear(&intAddRemove);
		eaIntersectAddr(&fg->avoidAdd, &fg->avoidRemove, &intAddRemove);

		eaClear(&diffAddRemove);
		eaDiffAddr(&fg->avoidAdd, &fg->avoidRemove, &diffAddRemove);

		eaClear(&diffRemoveAdd);
		eaDiffAddr(&fg->avoidRemove, &fg->avoidAdd, &diffRemoveAdd);

		for(i=eaSize(&intAddRemove)-1; i>=0; i--)
		{
			aiAvoidEntryRemove(intAddRemove[i]);
		}

		eaPushEArray(&toBG->avoidAdd, &diffAddRemove);
		eaClear(&fg->avoidAdd);
				
		eaPushEArray(&toBG->avoidRemove, &diffRemoveAdd);
#ifdef AI_PARANOID_AVOID
		for(i=eaSize(&diffRemoveAdd)-1; i>=0; i--)
		{
			devassert(diffRemoveAdd[i]->remTimeStamp);
		}
#endif
		eaClear(&fg->avoidRemove);
	}

#ifdef AI_PARANOID_AVOID
	fg->timeStamp++;
	toBG->timeStampFromFG = fg->timeStamp;
#endif
	ZeroStruct(&fg->updated);
}

static void aiSmoothRotationHandle(const MovementRequesterMsg *msg, AIMovementBG *bg, Quat faceRot)
{
	AIMovementRotationHandlerSmooth *smooth = (AIMovementRotationHandlerSmooth*)bg->rotationHandler;
	U32 procCount = 0;
	mrmGetProcessCountBG(msg, &procCount);

	unitQuat(smooth->_base.faceOut);

	if(smooth->lastSteadyState == 0)
	{
		smooth->lastSteadyState = procCount;
		copyQuat(faceRot, smooth->_base.faceOut);
		copyQuat(faceRot, smooth->steadyStateFacing);
	}
	else
	{
		F32 angleDiff = quatAngleBetweenQuats(smooth->steadyStateFacing, faceRot);

		if(bg->doMove && angleDiff < 0.05)
		{
			smooth->lastSteadyState = procCount;
			copyQuat(smooth->steadyStateFacing, smooth->_base.faceOut);
		}
		else
		{
			F32 interpTime = 0;
			F32 interpRatio = 0;
			F32 forgiveness = 0.1 * MM_PROCESS_COUNTS_PER_SECOND;
			U32 procDiff;
			Quat tmp;
			if(angleDiff < PI/2)
				interpTime = 0.3 * MM_PROCESS_COUNTS_PER_SECOND;
			else
				interpTime = 0.1 * MM_PROCESS_COUNTS_PER_SECOND;

			procDiff = procCount - smooth->lastSteadyState;
			if(procDiff < forgiveness)
				procDiff = 0;
			else
				procDiff -= forgiveness;
			interpRatio = 1.0 * procDiff / interpTime;
			MINMAX1(interpRatio, 0.0, 1.0);

			if(procDiff < interpTime / 2)
				interpRatio = interpRatio * interpRatio * 2;
			else
				interpRatio = sqrt(interpRatio * 4) / 2;

			// Update output
			quatInterp(interpRatio, smooth->steadyStateFacing, faceRot, tmp);
			copyQuat(tmp, smooth->_base.faceOut);

			// Update steady state
			quatInterp(interpRatio * interpRatio, smooth->steadyStateFacing, faceRot, tmp);
			copyQuat(tmp, smooth->steadyStateFacing);
		}
	}
}

AIMovementRotationHandler* aiMovementCreateRotationHandler(void)
{
	AIMovementRotationHandlerSmooth *handler = callocStruct(AIMovementRotationHandlerSmooth);

	handler->_base.func = aiSmoothRotationHandle;

	return (AIMovementRotationHandler*)handler;
}

// This function is one of several handlers.  It's basically a big virtual table for a plug-in system.
// It can be called from either the foreground or the background thread.
void aiMovementMsgHandler(const MovementRequesterMsg* msg)
{
	AIMovementFG*		fg;
	AIMovementBG*		bg;
	AIMovementLocalBG*	localBG;
	AIMovementToFG*		toFG;
	AIMovementToBG*		toBG;
	AIMovementSync*		sync;

	MR_MSG_HANDLER_GET_DATA_DEFAULT(msg, AIMovement);

	switch(msg->in.msgType)
	{
		xcase MR_MSG_FG_UPDATED_TOFG:
		{
			aiMovementHandleUpdatedToFG(msg, fg, toFG);
		}

		xcase MR_MSG_FG_CREATE_TOBG:
		{
			aiMovementHandleCreateToBG(msg, fg, toBG);
		}

		xcase MR_MSG_FG_BEFORE_DESTROY:
		{
#ifdef AI_PARANOID_AVOID
			int i, j;
			AIVolumeEntry **checkEntries = NULL;
			AIVolumeEntry ****checkEntryArrays = NULL;
			if(toFG)
			{
				eaPush(&checkEntryArrays, &toFG->avoidDestroy);
			}

			if(fg)
			{
				eaPush(&checkEntryArrays, &fg->avoidAdd);
			}

			if(toBG)
			{
				eaPush(&checkEntryArrays, &toBG->avoidAdd);
			}

			if(bg)
			{
				eaPush(&checkEntryArrays, &bg->avoidEntries);
			}

			for(i=0; i<eaSize(&checkEntryArrays); i++)
			{
				AIVolumeEntry ***check1 = checkEntryArrays[i];

				for(j=i+1; j<eaSize(&checkEntryArrays); j++)
				{
					AIVolumeEntry ***check2 = checkEntryArrays[j];

					eaClear(&checkEntries);
					eaIntersectAddr(check1, check2, &checkEntries);
					devassert(!eaSize(&checkEntries));
				}
			}
#endif
			if(toFG)
			{
				eaDestroy(&toFG->wpBadConn);
				eaDestroy(&toFG->wpBadConnReset);
				eaDestroy(&toFG->wpPassed);
				eaDestroyExFileLine(&toFG->wpDestroy, destroyNavPathWaypointEx);
				// wpDebugCurPath only points at waypoints in other paths
				eaDestroy(&toFG->wpDebugCurPath);
				eaDestroyEx(&toFG->animFxDestroy, aiMovementAnimFXHandleDestroy);
				eaDestroyEx(&toFG->avoidDestroy, aiAvoidEntryRemove);

				eaDestroyEx(&toFG->logEntries, aiDebugLogEntryDestroy);
			}
			if(fg)
			{
				navPathClear(&fg->pathNormal);
				navPathClear(&fg->pathRefine);
				eaDestroy(&fg->wpPassed);
				eaDestroyExFileLine(&fg->wpDestroy, destroyNavPathWaypointEx);
				eaDestroyEx(&fg->animFxAdd, aiMovementAnimFXHandleDestroy);
				eaDestroyEx(&fg->animFxCancel, aiMovementAnimFXHandleDestroy);
				eaDestroyEx(&fg->avoidAdd, aiAvoidEntryRemove);
				ea32Destroy(&fg->animHold);
				eaDestroy(&fg->avoidRemove);
			}
			if(toBG)
			{
				eaDestroyExFileLine(&toBG->waypointsPath, destroyNavPathWaypointEx);
				eaDestroyExFileLine(&toBG->waypointsRefine, destroyNavPathWaypointEx);
				eaDestroyEx(&toBG->animFxAdd, aiMovementAnimFXHandleDestroy);
				eaDestroyEx(&toBG->animFxCancel, aiMovementAnimFXHandleDestroy);
				eaDestroyEx(&toBG->avoidAdd, aiAvoidEntryRemove);
				ea32Destroy(&toBG->animHold);
				eaDestroy(&toBG->avoidRemove);
			}
			if(bg)
			{
				eaDestroyExFileLine(&bg->path.waypoints, destroyNavPathWaypointEx);
				eaDestroyEx(&bg->animFxList, aiMovementAnimFXHandleDestroy);
				eaDestroyEx(&bg->avoidEntries, aiAvoidEntryRemove);
				ea32Destroy(&toBG->animHold);
				SAFE_FREE(bg->rotationHandler);
			}
		}
		
		xcase MR_MSG_BG_INITIALIZE:
		{
			bg->rotationHandler = aiMovementCreateRotationHandler();
		}
		
		xcase MR_MSG_BG_DATA_RELEASE_REQUESTED:
		{
			msg->out->bg.dataReleaseRequested.denied = 1;
		}
		
		xcase MR_MSG_BG_GET_DEBUG_STRING:
		{
			// MS: Hey Someone, please fill this in!

			char*	buffer = msg->in.bg.getDebugString.buffer;
			U32		bufferLen = msg->in.bg.getDebugString.bufferLen;

			snprintf_s(	buffer,
						bufferLen,
						"");
		}

		xcase MR_MSG_BG_UPDATED_TOBG:
		{
			int i;
			AIMovementUpdateToFG oldUpdateToFG;
			U32 oldDoMove = bg->doMove;
			AIMovementRotationType oldRotate = bg->rotation.type;
			//U32 pc;
			//mrmGetProcessCountBG(msg, &pc);

			oldUpdateToFG = bg->updateToFG;

			mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			
			if(toBG->updated.debugForceProcess)
			{
				toBG->updated.debugForceProcess = 0;

				bg->debugForceProcess = toBG->debugForceProcess;
			}

			if(toBG->updated.doorComplete)
			{
				NavPath *path = &bg->path;
				NavPathWaypoint *wp = eaGet(&path->waypoints, path->curWaypoint);

				if(!wp)
				{
					// AI Movement might have found another path for some reason
				}
				else
				{
					aiMovementReachedWaypoint(msg, path, wp, bg, toFG, true);
					path->curWaypoint++;
				}

				bg->runningDoor = 0;
			}

			if(toBG->updated.sleeping)
			{
				bg->sleeping = toBG->sleeping;
			}

			if(toBG->updated.flying)
			{
				bg->flying = toBG->flying;
			}

			if(toBG->updated.teleportDisabled)
			{
				bg->teleportDisabled = toBG->teleportDisabled;
				toBG->updated.teleportDisabled = 0;
			}

			if(toBG->updated.rotationDisabled)
			{
				bg->rotationDisabled = toBG->rotationDisabled;
				toBG->updated.rotationDisabled = 0;
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
			
			bg->alwaysFlying = toBG->alwaysFlying;
			bg->canFly = toBG->canFly;

			if(bg->debugForceProcess)
			{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
				mrmEnableMsgUpdatedToFG(msg);
			}

			if(toBG->updated.path)
			{
				bg->doMove = toBG->doMove;
				if(bg->doMove){
					bg->doMoveDone = 0;
				}
				bg->recentMinDistToTarget = 0;
				bg->skipStuckDetection = 1;
				bg->timeLastShortcutCheck = 0;
				bg->timeLastReachableCheck = 0;
				bg->targetUnreachable = 0;

				// a path reset implies your movement is complete
				bg->updateToFG.movementCompleted = !bg->doMove;
				toFG->updateToFG.movementCompleted = bg->updateToFG.movementCompleted;
				toFG->updateToFG.updated.movementCompleted = true;
				mrmEnableMsgUpdatedToFG(msg);
				AI_DEBUG_PRINT_TAG_BG(AI_LOG_MOVEMENT, 6, AIMLT_MOVCOM, "%s:%d - UpdateToFG: %d", __FILE__, __LINE__, bg->updateToFG.movementCompleted);

				if(toBG->pathReset)
				{
					mrmGetPositionBG(msg, bg->lastPos);
					bg->path.circular = toBG->circular;
					bg->path.pingpong = toBG->pingpong;
					bg->path.pingpongRev = toBG->pingpongRev;
					bg->path.curWaypoint = toBG->curWaypoint;
					mrmEnableMsgUpdatedToFG(msg);
					if(eaSize(&bg->path.waypoints))
					{
						FOR_EACH_IN_EARRAY(bg->path.waypoints, NavPathWaypoint, wp)
						{
							if(globalWPParanoia)
								assert(wp->dts.dts==DTS_INBG);
							dtsSetState(&wp->dts, DTS_TOFG);
						}
						FOR_EACH_END

						eaPushEArray(&toFG->wpDestroy, &bg->path.waypoints);
						eaClear(&bg->path.waypoints);
					}
					eaClear(&toFG->wpPassed);

					if(eaSize(&toBG->waypointsPath))
					{
						assert(eaSize(&bg->path.waypoints)==0);

						FOR_EACH_IN_EARRAY(toBG->waypointsPath, NavPathWaypoint, wp)
						{
							if(globalWPParanoia)
								assert(wp->dts.dts==DTS_TOBG);
							dtsSetState(&wp->dts, DTS_INBG);
						}
						FOR_EACH_END
						
						eaPushEArray(&bg->path.waypoints, &toBG->waypointsPath);
						bg->updateToFG.movementCompleted = 0;  // But having new waypoints implies movement isn't done
						toFG->updateToFG.movementCompleted = bg->updateToFG.movementCompleted;
						toFG->updateToFG.updated.movementCompleted = true;
						mrmEnableMsgUpdatedToFG(msg);
						AI_DEBUG_PRINT_TAG_BG(AI_LOG_MOVEMENT, 6, AIMLT_MOVCOM, "%s:%d - UpdateToFG: %d", __FILE__, __LINE__, bg->updateToFG.movementCompleted);

						eaClear(&toBG->waypointsPath);

						assert(bg->path.curWaypoint >=0 && (int)bg->path.curWaypoint < eaSize(&bg->path.waypoints));
					}
				}

				bg->refineRequested = 0;
				toBG->pathReset = 0;
				bg->id = toBG->id;
			}

			if(toBG->updated.followTargetPos)
			{
				bg->timeLastShortcutCheck = 0;
			}

			if(toBG->updated.orders)
			{
				bg->orders = toBG->orders;

				bg->usePosLastTargetDistCheck = 0;
				bg->reachedOffset = false;
				zeroVec3(bg->posLastTargetDistCheck);
#ifdef AI_MOVEMENTTARGET_PARANOID
				devassert(bg->orders.movementType!=AI_MOVEMENT_ORDER_NONE || 
					!toBG->doMove || !vec3IsZero(toBG->target));
#endif
			}

			if(toBG->updated.metaorders)
			{
				bg->metaorders = toBG->metaorders;
			}

			if(toBG->updated.config)
				bg->config = toBG->config;

			if(toBG->updated.rotation)
				bg->rotation = toBG->rotation;

			if(toBG->updated.animBits)
			{
				int j;

				for(i = eaSize(&toBG->animFxCancel) - 1; i >= 0; i--)
				{
					int found = false;
					for(j = eaSize(&bg->animFxList) - 1; j >= 0; j--)
					{
						if(toBG->animFxCancel[i]->aiMovementHandle == bg->animFxList[j]->aiMovementHandle)
						{
							devassertmsg(toBG->animFxCancel[i]->name == bg->animFxList[j]->name, "Handle and name of AnimFX to cancel don't match");
							if(bg->animFxList[j]->mmfxHandle)
								mmrFxDestroyBG(msg, &bg->animFxList[j]->mmfxHandle);
							mrmEnableMsgUpdatedToFG(msg);
							eaPush(&toFG->animFxDestroy, eaRemoveFast(&bg->animFxList, j));
							found = true;
							break;
						}
					}

					if(!found)
					{
						for(j = eaSize(&toBG->animFxAdd) - 1; j >= 0; j--)
						{
							if(toBG->animFxCancel[i]->aiMovementHandle == toBG->animFxAdd[j]->aiMovementHandle)
							{
								toBG->animFxAdd[j]->destroyAfterOneTick = 1;
								found = true;
								break;
							}
						}
					}
					//devassertmsg(found, "Didn't find animfxhandle to cancel");
				}
				mrmEnableMsgUpdatedToFG(msg);
				eaPushEArray(&toFG->animFxDestroy, &toBG->animFxCancel);
				eaSetSize(&toBG->animFxCancel, 0);

				eaPushEArray(&bg->animFxList, &toBG->animFxAdd);
				eaSetSize(&toBG->animFxAdd, 0);
				
				if(eaSize(&bg->animFxList)){
					mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
					mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
				}else{
					mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
				}
			}

			if(toBG->updated.refine)
			{
				int insertIdx = toBG->refineInsertWp ? eaFind(&bg->path.waypoints, toBG->refineInsertWp) : 0;

				if (0 <= insertIdx
					&&
					(	!insertIdx ||
						insertIdx < eaSize(&bg->path.waypoints))
					&&
					(	!toBG->refineInsertReverse && bg->path.curWaypoint <= insertIdx ||
						toBG->refineInsertReverse && insertIdx <= bg->path.curWaypoint))
				{
					if (eaSize(&bg->path.waypoints))
					{
						while (	eaSize(&toBG->waypointsRefine) &&
								distance3(toBG->waypointsRefine[0]->pos, bg->path.waypoints[insertIdx]->pos) < 0.1f)
						{
							// in general we get here due to the ray cast used for AI not being consistent
							// 1st example: you can shoot a ray where a capsule can't move so the capsule will get stuck but the ray will keep refining its path
							// 2nd example: the AI code appears to use rays for many test, but they are not all setup with the same start and end point
							// hopefully, when this is true the bg stuck counter has been going up so the entity can teleport out

							NavPathWaypoint* wp = toBG->waypointsRefine[0];
							eaRemove(&toBG->waypointsRefine, 0);
							if(globalWPParanoia)
								assert(wp->dts.dts==DTS_TOBG);
							dtsSetState(&wp->dts, DTS_FREE);
							destroyNavPathWaypoint(wp);

							// Helpful to determine if something gets stuck due to this bug : printfColor(COLOR_RED, "Removing refine waypoint that's identical to current position!\n");
						}
					}

					if (eaSize(&toBG->waypointsRefine))
					{
						if(bg->path.curWaypoint == -1) {
							bg->path.curWaypoint = 0; // We finished a previous path, so reset before inserting
						}

						FOR_EACH_IN_EARRAY(toBG->waypointsRefine, NavPathWaypoint, wp)
						{
							if(globalWPParanoia)
								assert(wp->dts.dts==DTS_TOBG);
							dtsSetState(&wp->dts, DTS_INBG);
						}
						FOR_EACH_END

						if(!toBG->refineInsertReverse) {
							eaInsertEArray(&bg->path.waypoints, &toBG->waypointsRefine, insertIdx);
						} else {
							devassert(insertIdx+1 <= eaSize(&bg->path.waypoints)); // should we get here when size == 0, we'd have a problem with eaInsert doing nothing then the insert list leaking memory shortly below, but I don't think it's possible to reach that state
							bg->path.curWaypoint += eaSize(&toBG->waypointsRefine);
							eaReverse(&toBG->waypointsRefine);
							eaInsertEArray(&bg->path.waypoints, &toBG->waypointsRefine, insertIdx + 1);
						}

						// watch out for cases where the refinement is getting something stuck then refining again
						// when this happens, these settings can lock out stuck detection						

						bg->stuckCounter = 0;
						bg->recentMinDistToTarget = 0;
						bg->skipStuckDetection = 1;
						bg->doMove = 1;
						bg->doMoveDone = 0;
						bg->updateToFG.movementCompleted = 0;
						toFG->updateToFG.movementCompleted = bg->updateToFG.movementCompleted;
						toFG->updateToFG.updated.movementCompleted = true;
						mrmEnableMsgUpdatedToFG(msg);

						bg->targetUnreachable = 0;
						bg->timeLastShortcutCheck = 0;
						bg->timeLastReachableCheck = 0;

						AI_DEBUG_PRINT_TAG_BG(	AI_LOG_MOVEMENT, 6, AIMLT_MOVCOM,
												"%s:%d - UpdateToFG: %d",
												__FILE__, __LINE__, bg->updateToFG.movementCompleted);

						mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
					}
				}
				else
				{
					eaDestroyExFileLine(&toBG->waypointsRefine, destroyNavPathWaypointEx);
				}

				bg->refineRequested = 0;

				if(toBG->refineAgain) {
					bg->timeLastRefineCheck = 0;
				}

				eaClear(&toBG->waypointsRefine);
			}

			if(toBG->updated.animHold)
			{
				if(ea32Size(&bg->animHold)>0 && ea32Size(&toBG->animHold)==0)
					bg->animHoldClear = 1;
				ea32Copy(&bg->animHold, &toBG->animHold);
			}

			if(toBG->updated.avoid)
			{
#ifdef AI_PARANOID_AVOID
				{
					AIVolumeEntry **changes = NULL;
					eaDiffAddr(&toBG->avoidRemove, &toBG->avoidAdd, &changes);
					devassert(eaSize(&toBG->avoidRemove)==eaSize(&changes));

					eaClear(&changes);
					eaDiffAddr(&toBG->avoidAdd, &toBG->avoidRemove, &changes);
					devassert(eaSize(&toBG->avoidAdd)==eaSize(&changes));

					eaDestroy(&changes);
				}
#endif

				for(i = eaSize(&toBG->avoidRemove)-1; i >= 0; i--)
				{
					AIVolumeEntry *entry = toBG->avoidRemove[i];
					int findResult;
					findResult = eaFindAndRemoveFast(&bg->avoidEntries, entry);
#ifdef AI_PARANOID_AVOID
					devassert(eaFind(&toFG->avoidDestroy, entry)==-1);
					devassert(findResult!=-1);			// Something being removed before or without being added
					ASSERT_TRUE_AND_RESET(entry->inBG); // Same
					eaPush(&bg->removes, entry);		// Just tracking order of removes
					devassert(eaFind(&bg->avoidEntries, entry)==-1);  // Entry added twice somewhere
#endif
					mrmEnableMsgUpdatedToFG(msg);
					eaPush(&toFG->avoidDestroy, entry);
				}
				eaClear(&toBG->avoidRemove);

				if (!toBG->config.ignoreAvoid)
				{
					if(eaSize(&toBG->avoidAdd))
					{
						eaPushEArray(&bg->avoidEntries, &toBG->avoidAdd);
						for(i = eaSize(&toBG->avoidAdd)-1; i >= 0; i--)
						{
							AIVolumeEntry *entry = toBG->avoidAdd[i];
							if(aiAvoidEntryCheckSelf(NULL, msg, entry))
								aiMovementResolveAvoid(msg, bg, toFG);
#ifdef AI_PARANOID_AVOID
							// If this asserts, something is being added twice.
							ASSERT_FALSE_AND_SET(entry->inBG);
							eaPush(&bg->adds, entry);
#endif
						}
					}
				}
				eaClear(&toBG->avoidAdd);
			}

#ifdef AI_PARANOID_AVOID
			// If this asserts, somehow the toBGs are coming in out of order
			devassert(toBG->timeStampFromFG>bg->lastTimeStampFromFG || bg->lastTimeStampFromFG==0);
			bg->lastTimeStampFromFG = toBG->timeStampFromFG;
#endif

			ZeroStruct(&toBG->updated);

			if(bg->doMove || bg->doMove!=oldDoMove)
			{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}

			if(ea32Size(&bg->animHold) || bg->animHoldClear)
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);

			if(aiMovementWantRotation(msg, bg) || bg->rotation.type!=oldRotate)
			{
				mrmHandledMsgsAddBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
			}
			
			if(TRUE_THEN_RESET(toFG->enabled)){
				mrmEnableMsgUpdatedToFG(msg);
			}

			if(bg->sleeping) // Regardless of what else goes on, don't do discuss if you sleep
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}

		xcase MR_MSG_BG_DISCUSS_DATA_OWNERSHIP:
		{
			aiMovementHandleDiscussDataOwnership(msg, bg, toFG);

			if(!bg->doMove && bg->rotation.type==AI_MOVEMENT_ROTATION_NONE)
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_DISCUSS_DATA_OWNERSHIP);
		}

		xcase MR_MSG_BG_CREATE_DETAILS:
		{
			int i;

			if(ea32Size(&bg->animHold) || bg->animHoldClear)
				return;

			for(i = eaSize(&bg->animFxList)-1; i >= 0; i--)
			{
				AIMovementAnimFXHandle* handle = bg->animFxList[i];

				if(handle->bitHandle)
					mrmAnimAddBitBG(msg, handle->bitHandle);
				else if(!handle->mmfxHandle)
				{
					MMRFxConstant fxConstant = {0};

					fxConstant.fxName = handle->name;
					if(!handle->destroyAfterOneTick)
						mmrFxCreateBG(msg, &handle->mmfxHandle, &fxConstant, NULL);
					else
						mmrFxCreateBG(msg, NULL, &fxConstant, NULL);
				}

				if(handle->destroyAfterOneTick)
				{
					eaPush(&toFG->animFxDestroy, eaRemoveFast(&bg->animFxList, i));
					mrmEnableMsgUpdatedToFG(msg);
				}
			}
			
			if(!eaSize(&bg->animFxList)){
				mrmHandledMsgsRemoveBG(msg, MR_HANDLED_MSG_CREATE_DETAILS);
			}
		}
		
		xcase MR_MSG_BG_CREATE_OUTPUT:
		{
			aiMovementHandleCreateOutput(msg, bg, toFG);

			if(TRUE_THEN_RESET(toFG->enabled)){
				mrmEnableMsgUpdatedToFG(msg);
			}
		}
	}
}

F32 aiMovementGetPathEntHeight(Entity *e)
{
	F32 height = entGetHeight(e);

	if(height<20)
		height = 0;  

	return height;
}

static AIMovementFG* aiMovementGetFG(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib)
{
	AIMovementFG* fg = NULL;
	
	if(mrGetFG(aib->movement, aiMovementMsgHandler, &fg))
	{
		return fg;
	}

	aiMovementCreate(&aib->movement, e->mm.movement);

	if(mrGetFG(aib->movement, aiMovementMsgHandler, &fg))
	{
		return fg;
	}
	
	devassertmsg(0, "Couldn't create movement requester, something is horribly wrong");

	return NULL;
}

NavPathWaypoint* aiMovementGetTargetWp(Entity *e, AIVarsBase *aib)
{
	AIMovementFG *fg = aiMovementGetFG(e, aib);

	if(fg)
	{
		return navPathGetTargetWaypoint(&fg->pathNormal);
	}

	return NULL;
}

void aiMovementCreate(MovementRequester** mrOut, MovementManager* mm)
{
	AIMovementFG* fg;

	if(mmRequesterGetByNameFG(mm, "AIMovement", mrOut))
		return;

	mmRequesterCreateBasic(mm, mrOut, aiMovementMsgHandler);
	devassert(*mrOut);
	mrGetFG(*mrOut, aiMovementMsgHandler, &fg);
	devassert(fg);
}

void aiMovementDestroy(MovementRequester** mrInOut)
{
	mrDestroy(mrInOut);
}

#define AI_DEBUG_LOG_PATH_DEBUG_LVL 5

void aiMovementTagFGForPathUpdateEx(Entity* be, AIVarsBase* aib, AIMovementFG* fg, const char* file, int line)
{
	fg->updated.path = 1;
	mrEnableMsgCreateToBG(aib->movement);
	AI_DEBUG_PRINT(be, AI_LOG_MOVEMENT, AI_DEBUG_LOG_PATH_DEBUG_LVL, "FG tagged for update %s:%d", file, line);

	entSetActive(be);

#ifdef AI_MOVEMENTTARGET_PARANOID
	devassert(!fg->doMove || fg->orders.movementType==AI_MOVEMENT_ORDER_NONE || !vec3IsZero(fg->orders.target));
#endif
}

void aiDebugLogPath(Entity* e, NavPath* path)
{
	int i, n;
	char* beaconStr = NULL;
	estrStackCreate(&beaconStr);

	n = eaSize(&path->waypoints);
	for(i = 0; i < n; i++)
	{
		NavPathWaypoint* wp = path->waypoints[i];
		estrPrintf(&beaconStr, "Beacon %i, Pos: " LOC_PRINTF_STR ", ConnectType: ", i, vecParamsXYZ(wp->pos));
		switch(wp->connectType)
		{
		xcase NAVPATH_CONNECT_ATTEMPT_SHORTCUT:
			estrConcatf(&beaconStr, "AttemptShortcut");
		xcase NAVPATH_CONNECT_GROUND:
			estrConcatf(&beaconStr, "Ground");
		xcase NAVPATH_CONNECT_JUMP:
			estrConcatf(&beaconStr, "Jump");
		xcase NAVPATH_CONNECT_FLY:
			estrConcatf(&beaconStr, "Fly");
		xcase NAVPATH_CONNECT_WIRE:
			estrConcatf(&beaconStr, "Wire");
		xcase NAVPATH_CONNECT_ENTERABLE:
			estrConcatf(&beaconStr, "Enterable");
		}
		AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, AI_DEBUG_LOG_PATH_DEBUG_LVL, "%s", beaconStr);
	}
	
	estrDestroy(&beaconStr);
}

// Assumes source and target are 2ft off ground
static int aiMovementMakePath(	Entity* e, AIVarsBase* aib, NavPath *navPath, const Vec3 source, 
								const Vec3 target, int* canShortcut, int *didPathfindOut,
								NavSearchResultType *resultOut,
								U32 inAvoid, AIMovementTargetFlags flags)
{
	NavSearchResultType result;
	AIMovementFG* fg;
	S32 iPartitionIdx;
	FSMStateTrackerEntry *tracker;
	FSMLDGenericU64 *pathfinddata = NULL;

	PERFINFO_AUTO_START_FUNC();

	fg = aiMovementGetFG(e, aib);
	iPartitionIdx = entGetPartitionIdx(e);

	navPathClear(navPath);

	if(canShortcut)
		*canShortcut = false;
	if(didPathfindOut)
		*didPathfindOut = true;

	tracker = SAFE_MEMBER2(aib, fsmContext, curTracker);
	if(tracker)
		pathfinddata = getMyDataFromDataIfExists(&tracker->localData, parse_FSMLDGenericU64, (U64)(intptr_t)aiMovementMakePath);

	if(distance3SquaredXZ(source, target)>SQR(1000) || g_ForceFailPathfindDist)
	{
		static int ignore = -1;

		if(ignore<0) 
			ignore = !!zmapInfoHasSpaceRegion(NULL) || combatBeaconArray.size==0;

		if (!ignore && (!e->erOwner || !entIsPlayer(entFromEntityRef(iPartitionIdx, e->erOwner))))
		{
			const char* zmapName = zmapInfoGetPublicName( NULL );
			const char *encName = e->pCritter ? critter_GetEncounterName(e->pCritter) : NULL;

			ErrorDetailsf("%s (%s) "LOC_PRINTF_STR" to "LOC_PRINTF_STR, ENTDEBUGNAME(e), encName, vecParamsXYZ(source), vecParamsXYZ(target));
			if( !resNamespaceIsUGC( zmapName )) {
				Errorf( "Zonemap: %s -- Entity attempted pathfind over legal limit.  You should likely use a Patrol (or more Patrol Points).  Or speak to the AI team.", zmapName );
			} else {
				Errorf( "UGC Zonemap -- Entity attempted pathfind over legal limit.  If this is bad, you should talk to the UGC team about how to make this not an issue." );
			}
			
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	if (pathfinddata && pathfinddata->myU64 > 5)
	{
		PERFINFO_AUTO_STOP();
		return false;
	}

	if (fg->config.collisionlessMovement ||
		(	!(flags & AI_MOVEMENT_TARGET_DONT_SHORTCUT)	&&
			!globalDisableShortcutting					&& 
			!aiCollideRay(iPartitionIdx, e, source, NULL, target, STD_AICOLLIDERAY_FLAGS(fg->flying, inAvoid))))
	{
		NavPathWaypoint *wp = createNavPathWaypoint();

		wp->targetWp = 1;
		wp->connectType = NAVPATH_CONNECT_ATTEMPT_SHORTCUT;
		copyVec3(target, wp->pos);	
		navPathAddTail(navPath, wp);

		if(canShortcut)
			*canShortcut = true;
	}
	else
	{
		if(canShortcut)
			*canShortcut = false;

		if (flags & AI_MOVEMENT_TARGET_CRITICAL || ABS_TIME_SINCE_PARTITION(iPartitionIdx, aib->time.lastPathFind) > SEC_TO_ABS_TIME(3))
		{
			aib->time.lastPathFind = ABS_TIME_PARTITION(iPartitionIdx);

			beaconSetPathFindEntity(entGetRef(e), 0, aiMovementGetPathEntHeight(e));
			beaconSetPathFindEntityUseAvoid(inAvoid || fg->config.ignoreAvoid);
			result = beaconPathFind(iPartitionIdx, navPath, source, target, ENTDEBUGNAME(e));

			AI_DEBUG_PRINT(	e, AI_LOG_MOVEMENT, 3,
							"Pathfind from " LOC_PRINTF_STR " to " LOC_PRINTF_STR " %s",
							vecParamsXYZ(source), vecParamsXYZ(target), result == NAV_RESULT_SUCCESS ? "succeeded" : "failed");

			if (gConf.bDisableSuperJump && result == NAV_RESULT_TARGET_UNREACHABLE)
			{
				// This case enables a new form of super-jumping to avoid exploits.  Right now, I turn it on only if the old super-jumping
				// is disabled. This doesn't have to be the case.  If we need to change the logic that gets you to this point in the code,
				// let me know.  [RMARR - 5/13/13]
				beaconSetPathFindEntity(entGetRef(e), 90.0f, aiMovementGetPathEntHeight(e));
				result = beaconPathFind(iPartitionIdx, navPath, source, target, ENTDEBUGNAME(e));
			}

			if(resultOut)
				*resultOut = result;

			if(result == NAV_RESULT_TIMEOUT)
			{
				if(!pathfinddata && tracker)
					pathfinddata = getMyDataFromData(&tracker->localData, parse_FSMLDGenericU64, (U64)(intptr_t)aiMovementMakePath);

				if(pathfinddata)
					pathfinddata->myU64++;
			}

			if(result == NAV_RESULT_SUCCESS)
			{
				NavPathWaypoint* endWp = NULL;
				endWp = createNavPathWaypoint();
				endWp->connectType = NAVPATH_CONNECT_ATTEMPT_SHORTCUT;
				endWp->requestedRefine = true;
				endWp->targetWp = true;

				copyVec3(target, endWp->pos);
				navPathAddTail(navPath, endWp);
			}

			if(result == NAV_RESULT_NO_SOURCE_BEACON)
			{
				Beacon *b = beaconGetNearestBeacon(iPartitionIdx, source);

				if(b && !disableTeleport)
					entSetPos(e, b->pos, 1, "teleport to nearest beacon");
			}
			else if(result != NAV_RESULT_SUCCESS)
			{
				PERFINFO_AUTO_STOP();
				return false;
			}
		}
		else
		{
			if(didPathfindOut)
				*didPathfindOut = false;
			PERFINFO_AUTO_STOP();
			return false;
		}
	}

	FOR_EACH_IN_EARRAY(navPath->waypoints, NavPathWaypoint, wp)
	{
		if(globalWPParanoia)
			assert(wp->dts.dts==DTS_NONE);
		dtsSetState(&wp->dts, DTS_INFG);
	}
	FOR_EACH_END

	PERFINFO_AUTO_STOP();
	return true;
}

// Assumes source and target are 2ft off ground
static int aiMovementFindPath(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib, SA_PRE_NN_RELEMS(3) const Vec3 target,
							 SA_PRE_NN_FREE SA_POST_NN_VALID int* canShortcut, SA_PRE_NN_FREE SA_POST_NN_VALID int *didPathfindOut, NavSearchResultType *resultOut,
							 AIMovementTargetFlags flags)
{
	AIConfig* config = aiGetConfig(e, aib);
	Vec3 sourcePos;
	Vec3 targetPos;
	int ret;
	int inAvoid = false;
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	
	mrEnableMsgCreateToBG(aib->movement);

	*canShortcut = false;
	*didPathfindOut = true;

	entGetPos(e, sourcePos);

	copyVec3(target, targetPos);
	targetPos[1] += config->movementParams.movementYOffset; // adjusted for both target entities and locations, see calling function

	if(eaSize(&aib->avoid.base.volumeEntities))
	{
		int i;
		for(i = eaSize(&aib->avoid.base.volumeEntities)-1; i >= 0; i--)
		{
			AIVolumeEntry* entry = aib->avoid.base.volumeEntities[i];
			if(aiAvoidEntryCheckPoint(NULL, targetPos, entry, true, NULL))
			{
				char* avoidLoc = NULL;

				if(AI_DEBUG_ENABLED(AI_LOG_MOVEMENT, 3))
				{
					Vec3 pos;
					estrStackCreate(&avoidLoc);

					if(entry->entRef)
					{
						Entity *avoidEnt = entFromEntityRef(entGetPartitionIdx(e), entry->entRef);
						if(avoidEnt)
							entGetPos(avoidEnt, pos);
					}
					else if(entry->isSphere)
						estrPrintf(&avoidLoc, LOC_PRINTF_STR, vecParamsXYZ(entry->spherePos));
					else
						estrPrintf(&avoidLoc, LOC_PRINTF_STR, vecParamsXYZ(entry->boxMat[3]));

					AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 3, "Cannot move to " LOC_PRINTF_STR " because "
						" of avoid entry at %s", vecParamsXYZ(targetPos), avoidLoc);

					estrDestroy(&avoidLoc);
				}
				return false;
			}
			if(!inAvoid && aiAvoidEntryCheckPoint(e, NULL, entry, true, NULL))
				inAvoid = true;
		}
	}

	devassert(fg);

	ret = aiMovementMakePath(e, aib, &fg->pathNormal, sourcePos, targetPos, canShortcut, didPathfindOut, resultOut, inAvoid, flags);

	if (ret && AI_DEBUG_ENABLED(AI_LOG_MOVEMENT, AI_DEBUG_LOG_PATH_DEBUG_LVL))
		aiDebugLogPath(e, &fg->pathNormal);

	return ret;
}

// ------------------------------------------------------------------------------------------------------------------
int aiMovementSetWaypointsExplicit(Entity* e, const F32 *eafWaypoints)
{
	AIMovementFG* fg = aiMovementGetFG(e, e->aibase);
	S32 numWaypoints = eafSize(&eafWaypoints);
	S32 i;
	if (!numWaypoints || numWaypoints % 3 != 0)
		return 0;
	numWaypoints = numWaypoints/3;

	mrEnableMsgCreateToBG(e->aibase->movement);

	navPathClear(&fg->pathNormal);

	e->aibase->currentlyMoving = true;
	fg->doMove = 1;
	fg->updated.path = 1;
	fg->pathReset = 1;
	fg->orders.movementType = AI_MOVEMENT_ORDER_POS;
	
	AI_DEBUG_PRINT_TAG(e, AI_LOG_MOVEMENT, 4, AIMLT_CURMOV, "%s:%d Currently Moving %d", __FILE__, __LINE__, e->aibase->currentlyMoving);

	for (i = 0; i < numWaypoints; ++i)
	{
		NavPathWaypoint *wp = createNavPathWaypoint();
		const F32 *pvPos = eafWaypoints + i * 3;

		if (i+1 == numWaypoints)
		{
			// the last waypoint is the movement order's target
			copyVec3(pvPos, fg->orders.targetPos);
			wp->targetWp = 1;
		}

		wp->connectType = NAVPATH_CONNECT_ATTEMPT_SHORTCUT;
		copyVec3(pvPos, wp->pos);		
		navPathAddTail(&fg->pathNormal, wp);

		dtsSetState(&wp->dts, DTS_INFG);
	}

	return 1;
}

AUTO_COMMAND_QUEUED();
void aiMovementAvoidLeft(ACMD_POINTER Entity* e, ACMD_POINTER AIVarsBase *aib)
{
	aib->leavingAvoid = false;
}

AUTO_COMMAND_QUEUED();
void aiMovementResetPathEx(ACMD_POINTER Entity* e, ACMD_POINTER AIVarsBase* aib, const char* filename, int linenumber)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	U32 needUpdate = false;
	if (fg->orders.movementType!=AI_MOVEMENT_ORDER_NONE || fg->doMove)
	{
		needUpdate = true;

		AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, AI_DEBUG_LOG_PATH_DEBUG_LVL,
							"Path reset from %s: %d", filename, linenumber);

		fg->updated.orders = 1;
		ZeroStruct(&fg->orders);
		aiMovementSetMovementType(&fg->orders, AI_MOVEMENT_ORDER_NONE, 0);

		fg->pathReset = 1;
		fg->doMove = 0;

		navPathClear(&fg->pathNormal);
		navPathClear(&fg->pathRefine);

		aiMovementTagFGForPathUpdate(e, aib, fg);
	}

	if(fg->rotation.type!=AI_MOVEMENT_ROTATION_NONE)
	{
		needUpdate = true;

		ZeroStruct(&fg->rotation);
		fg->updated.rotation = 1;
	}

	if(needUpdate)
		mrEnableMsgCreateToBG(aib->movement);
}


// ------------------------------------------------------------------------------------------------------------------
int aiMovementGetTargetPosition(Entity *e, AIVarsBase *aib, Vec3 targetOut)
{
	AIMovementFG *fg = aiMovementGetFG(e, aib);

	if (!targetOut)
	{
		return fg->orders.movementType != AI_MOVEMENT_ORDER_NONE;
	}

	return aiMovementGetTargetPositionInternal(fg, targetOut);
}

int aiMovementSetTargetPositionEx(Entity* e, AIVarsBase* aib, const Vec3 target, S32 *didPathfindOut, AIMovementTargetFlags targetFlags, const char *func, const char *file, int line)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	Vec3 targetPos = {0};
	int iPartitionIdx = entGetPartitionIdx(e);

	if(didPathfindOut)
		*didPathfindOut = false;

	if(target)
	{
		int hitGround = 0;
		copyVec3(target, targetPos);
		worldSnapPosToGround(iPartitionIdx, targetPos, 3, -6, &hitGround);
		if(!hitGround)
			worldSnapPosToGround(iPartitionIdx, targetPos, 5, -10, &hitGround);
		targetPos[1] += 2;
	}

	if(fg->orders.movementType == AI_MOVEMENT_ORDER_POS && target && sameVec3(fg->orders.targetPos, targetPos))
	{
		if(fg->orders.pathfindResult)
			return true;

		if(zmapInfoGetMapType(NULL)==ZMTYPE_STATIC || 
			ABS_TIME_SINCE_PARTITION(iPartitionIdx, aib->time.lastPathFind)<SEC_TO_ABS_TIME(5))
			return fg->orders.pathfindResult;
	}

	mrEnableMsgCreateToBG(aib->movement);

	if(target)
	{
		Vec3 sourcePos;
		int canShortcut;
		int didPathfind;
		int pathResult;
		NavSearchResultType result;

		fg->updated.orders = 1;
		ZeroStruct(&fg->orders);
		aiMovementSetMovementType(&fg->orders, AI_MOVEMENT_ORDER_POS, 0);
		fg->orders.useOffset = 0;
		MM_CHECK_DYNPOS_DEVONLY(targetPos);
		copyVec3(targetPos, fg->orders.targetPos);

		AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 2, "Told to move to %.2f, %.2f, %.2f by %s (%s(%d))", vecParamsXYZ(targetPos), func, file, line);

		entGetPos(e, sourcePos);
		sourcePos[1] += 2;

		navPathClear(&fg->pathNormal);
		pathResult = aiMovementFindPath(e, aib, targetPos, &canShortcut, &didPathfind, &result, targetFlags);
		if(didPathfindOut)
			*didPathfindOut = didPathfind;

		if(!didPathfind)
		{
			fg->doMove = 0;
			fg->orders.pathfindResult = false;
			aiMovementSetMovementType(&fg->orders, AI_MOVEMENT_ORDER_NONE, 0);

			AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 2, "Did not path due to timeout: from "LOC_PRINTF_STR" to "LOC_PRINTF_STR, 
							vecParamsXYZ(sourcePos), vecParamsXYZ(targetPos));

			aiMovementTagFGForPathUpdate(e, aib, fg);
			return false;
		}
		else if(!pathResult)
		{
			fg->doMove = 0;
			fg->orders.pathfindResult = false;

			AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 2, "Pathfind failed to pos (%s): from "LOC_PRINTF_STR" to "LOC_PRINTF_STR, 
				StaticDefineIntRevLookup(NavSearchResultTypeEnum, result),
				vecParamsXYZ(sourcePos), vecParamsXYZ(targetPos));

			aiMovementTagFGForPathUpdate(e, aib, fg);
			return false;
		}
		else
			fg->orders.pathfindResult = true;

		fg->pathReset = 1;
		aib->currentlyMoving = 1;
		aib->leavingAvoid = false;
		copyVec3(sourcePos, fg->lastWPPos);
		fg->doMove = 1;
		fg->flags = targetFlags;
		fg->updated.rotation = 1;
		ZeroStruct(&fg->rotation);

		AI_DEBUG_PRINT_TAG(e, AI_LOG_MOVEMENT, 4, AIMLT_CURMOV, "%s:%d Currently Moving %d", __FILE__, __LINE__, aib->currentlyMoving);

		if(eaSize(&fg->pathNormal.waypoints))
		{
			assert(fg->pathNormal.curWaypoint>=0);
			assert((int)fg->pathNormal.curWaypoint < eaSize(&fg->pathNormal.waypoints));
		}

		if (e->pPlayer)
		{
			e->pPlayer->bMovingToLocation = aib->currentlyMoving;
			entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
		}
	}
	else
	{
		fg->doMove = 0;
		AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 2, "Told to stop moving - no pos target");
	}

	aiMovementTagFGForPathUpdate(e, aib, fg);
	return true;
}

void aiMovementSetFinalFacePos(Entity* be, AIVarsBase* aib, const Vec3 finalFacePos)
{
	AIMovementFG* fg = aiMovementGetFG(be, aib);

	mrEnableMsgCreateToBG(aib->movement);

	fg->updated.rotation = 1;
	ZeroStruct(&fg->rotation);

	if(finalFacePos)
	{
		fg->rotation.type = AI_MOVEMENT_ROTATION_POS;
		copyVec3(finalFacePos, fg->rotation.finalFacePos);
	}
}

void aiMovementSetFinalFaceRot(Entity* be, AIVarsBase* aib, const Quat finalFaceRot)
{
	AIMovementFG* fg = aiMovementGetFG(be, aib);

	fg->updated.rotation = 1;
	ZeroStruct(&fg->rotation);
	mrEnableMsgCreateToBG(aib->movement);

	if(finalFaceRot)
	{
		fg->rotation.type = AI_MOVEMENT_ROTATION_ROT;
		copyQuat(finalFaceRot, fg->rotation.finalFaceRot);
	}
}

void aiMovementSetFinalFaceEntity(Entity* be, AIVarsBase* aib, Entity* target)
{
	AIMovementFG* fg = aiMovementGetFG(be, aib);
	EntityRef targetRef = entGetRef(target);

	if(fg->rotation.type == AI_MOVEMENT_ROTATION_ENTREF &&
		fg->rotation.finalFaceEntRef == targetRef)
	{
		return;
	}

	fg->updated.rotation = 1;
	ZeroStruct(&fg->rotation);
	mrEnableMsgCreateToBG(aib->movement);

	if(target)
	{
		fg->rotation.type = AI_MOVEMENT_ROTATION_ENTREF;
		fg->rotation.finalFaceEntRef = targetRef;
	}
}

void aiMovementClearRotationTarget(Entity* be, AIVarsBase* aib)
{
	AIMovementFG* fg = aiMovementGetFG(be, aib);
	
	if(fg->rotation.type == AI_MOVEMENT_ROTATION_NONE)
	{
		return;
	}

	fg->updated.rotation = 1;
	ZeroStruct(&fg->rotation);
	mrEnableMsgCreateToBG(aib->movement);
}

void aiMovementUpdateBGNavSearchEx(Entity* e, AIVarsBase* aib, const char* file, int line)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	Vec3 sourcePos;

	devassert(eaSize(&fg->pathNormal.waypoints));

	entGetPos(e, sourcePos);

	fg->updated.orders = 1;

	ZeroStruct(&fg->orders);
	aiMovementSetMovementType(&fg->orders, AI_MOVEMENT_ORDER_NONE, 0);

	fg->pathReset = 1;
	aib->currentlyMoving = 1;
	aib->leavingAvoid = false;
	copyVec3(sourcePos, fg->lastWPPos);
	fg->doMove = 1;
	fg->updated.rotation = 1;
	ZeroStruct(&fg->rotation);

	AI_DEBUG_PRINT_TAG(e, AI_LOG_MOVEMENT, 4, AIMLT_CURMOV, "%s:%d Currently Moving %d", __FILE__, __LINE__, aib->currentlyMoving);

	if(eaSize(&fg->pathNormal.waypoints))
	{
		assert(fg->pathNormal.curWaypoint>=0);
		assert((int)fg->pathNormal.curWaypoint<eaSize(&fg->pathNormal.waypoints));
	}

	aiMovementTagFGForPathUpdateEx(e, aib, fg, file, line);

	if (e->pPlayer)
	{
		e->pPlayer->bMovingToLocation = aib->currentlyMoving;
		entity_SetDirtyBit(e, parse_Player, e->pPlayer, false);
	}
}

// Sets the settled flag for the AI_MOVEMENT_ORDER_ENT order
bool aiMovementSetTargetEntitySettledFlag(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase *aib, EntityRef targetRef, bool bSettled, Vec3 vecSettledPos)
{
	AIMovementFG* fg;

	devassert(e);
	devassert(aib);
	if (bSettled)
		devassert(vecSettledPos);
	else
		devassert(vecSettledPos == NULL);

	fg = aiMovementGetFG(e, aib);

	if (fg == NULL)
		return false;

	if (fg->orders.movementType != AI_MOVEMENT_ORDER_ENT ||
		fg->orders.targetRef != targetRef)
		return false;

	if (bSettled)
	{
		if (vecSettledPos == NULL || (fg->orders.settled && cmpVec3XYZ(vecSettledPos, fg->orders.settledPos) == 0))
		{
			return false;
		}

		// Copy the new settled pos
		fg->orders.settled = true;
		copyVec3(vecSettledPos, fg->orders.settledPos);
		fg->updated.orders = true;
		aiMovementTagFGForPathUpdate(e, aib, fg);
		return true;
	}
	else if (fg->orders.settled)
	{
		fg->orders.settled = false;
		fg->updated.orders = true;
		aiMovementTagFGForPathUpdate(e, aib, fg);
		return true;
	}

	return false;
}

int aiMovementSetTargetEntityEx(Entity* e, AIVarsBase* aib, Entity* target, const Vec3 offset, int offsetRotRelative,
								AIMovementOrderEntDetail detail, AIMovementTargetFlags flags, 
								const char *func, const char *file, int line)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	EntityRef targetRef = target ? entGetRef(target) : 0;

	if(fg->orders.movementType == AI_MOVEMENT_ORDER_ENT && 
		fg->orders.targetRef == targetRef && 
			(!offset && vec3IsZero(fg->orders.targetOffset) ||
			(offset && distance3Squared(offset, fg->orders.targetOffset)<SQR(0.5))))
	{
		if (aib->currentlyMoving)
		{
			if(fg->orders.pathfindResult)
				return fg->orders.pathfindResult;

			if(zmapInfoGetMapType(NULL)==ZMTYPE_STATIC || 
				ABS_TIME_SINCE_PARTITION(entGetPartitionIdx(e), aib->time.lastPathFind)<SEC_TO_ABS_TIME(5))
				return fg->orders.pathfindResult;
		}
		else
		{	// we're no longer moving, so make sure we're not too far from 
			Vec3 targetPos;
			if (aiMovementGetTargetPositionInternal(fg, targetPos))
			{
				Vec3 curPos;
				entGetPos(e, curPos);
				if (distance3Squared(curPos, targetPos) < SQR(CLOSE_ENOUGH_DIST))
				{
					return fg->orders.pathfindResult;
				}
			}
		}
	}

	if(target)
	{
		Vec3 sourcePos;
		Vec3 targetPos;
		int canShortcut;
		int didPathfind;
		int pathResult;
		NavSearchResultType result;

		fg->updated.orders = 1;
		ZeroStruct(&fg->orders);
		aiMovementSetMovementType(&fg->orders, AI_MOVEMENT_ORDER_ENT, detail);
		fg->orders.targetRef = targetRef;
		if(offset)
		{
			if (detail != AI_MOVEMENT_ORDER_ENT_GET_IN_RANGE_DIST)
			{
				copyVec3(offset, fg->orders.targetOffset);
				fg->orders.offsetRotRelative = !!offsetRotRelative;
				fg->orders.useOffset = 1;
			}
			else
			{
				copyVec3(offset, fg->orders.targetOffset);
				fg->orders.offsetRotRelative = !!offsetRotRelative;
			}

			fg->orders.stopWithinRange = (detail == AI_MOVEMENT_ORDER_ENT_GET_IN_RANGE_DIST || 
											detail == AI_MOVEMENT_ORDER_ENT_GET_IN_RANGE);
		}
		else
		{
			zeroVec3(fg->orders.targetOffset);
			fg->orders.offsetRotRelative = 0;
			fg->orders.useOffset = 0;
		}
		
		entGetPos(e, sourcePos);
		entGetPos(target, targetPos);
		if(fg->orders.offsetRotRelative)
		{
			Vec3 rotOffset;
			Quat rot;
			Vec3 pyFace;
			entGetFacePY(target, pyFace);
			yawQuat(-pyFace[1], rot);
			quatRotateVec3(rot, fg->orders.targetOffset, rotOffset);
			addVec3(targetPos, rotOffset, targetPos);
		}
		else
		{
			addVec3(targetPos, fg->orders.targetOffset, targetPos);
		}

		AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 2, "Told to follow %s(%d) by %s (%s(%d))", ENTDEBUGNAME(target), targetRef, func, file, line);

		navPathClear(&fg->pathNormal);
		sourcePos[1] += 2; targetPos[1] += 2;
		pathResult = aiMovementFindPath(e, aib, targetPos, &canShortcut, &didPathfind, &result, flags);

		if(!didPathfind)
		{
			fg->doMove = 0;
			fg->orders.pathfindResult = false;
			aib->currentlyMoving = false;
			aib->leavingAvoid = false;
			aiMovementSetMovementType(&fg->orders, AI_MOVEMENT_ORDER_NONE, 0);

			AI_DEBUG_PRINT_TAG(e, AI_LOG_MOVEMENT, 4, AIMLT_CURMOV, "%s:%d Currently Moving %d", __FILE__, __LINE__, aib->currentlyMoving);
			AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 2, "Did not path due to timeout: from "LOC_PRINTF_STR" to "LOC_PRINTF_STR, vecParamsXYZ(sourcePos), vecParamsXYZ(targetPos));

			aiMovementTagFGForPathUpdate(e, aib, fg);
		}
		else if(!pathResult)
		{
			fg->doMove = 0;
			fg->orders.pathfindResult = false;

			AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 2, "Pathfind failed to ent (%s): from "LOC_PRINTF_STR" to "LOC_PRINTF_STR, 
				StaticDefineIntRevLookup(NavSearchResultTypeEnum, result),
				vecParamsXYZ(sourcePos), vecParamsXYZ(targetPos));

			return false;
		}
		else
		{
			fg->orders.pathfindResult = true;
			aib->currentlyMoving = true;
			aib->leavingAvoid = false;

			AI_DEBUG_PRINT_TAG(e, AI_LOG_MOVEMENT, 4, AIMLT_CURMOV, "%s:%d Currently Moving %d", __FILE__, __LINE__, aib->currentlyMoving);
		}

		aiMovementSetFinalFaceEntity(e, aib, target);
		
		fg->flags = flags;
		fg->pathReset = 1;
		fg->doMove = 1;
		fg->updated.rotation = 1;
		ZeroStruct(&fg->rotation);
	}
	else
	{
		fg->doMove = 0;
		AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 2, "Told to stop moving - no ent target");
	}

	aiMovementTagFGForPathUpdate(e, aib, fg);

	return true;
}

void aiMovementClearMovementTarget(Entity* e, AIVarsBase* aib)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
		
	if(fg->orders.movementType == AI_MOVEMENT_ORDER_NONE && fg->doMove == 0)
	{
		return;
	}

	aiMovementSetMovementType(&fg->orders, AI_MOVEMENT_ORDER_NONE, 0);
	fg->doMove = 0;

	aiMovementTagFGForPathUpdate(e, aib, fg);
	return;
}

void aiMovementGoToSpawnPosEx(Entity* e, AIVarsBase* aib, AIMovementTargetFlags flags, const char* func, const char* file, int line)
{
	Vec3 pos;
	Entity* owner = e->erOwner ? entFromEntityRef(entGetPartitionIdx(e), e->erOwner) : NULL;
	AITeam* team = aiTeamGetCombatTeam(e, aib);
	int foundPath = false;
	S32 didPathfind = false;

	aiGetSpawnPos(e, aib, pos);

	if(owner)
	{
		foundPath = aiMovementSetTargetEntityEx(e, aib, owner, aib->spawnOffset, 0, AI_MOVEMENT_ORDER_ENT_FOLLOW, flags, func, file, line);
		if(!foundPath)
		{
			entSetPos(e, pos, true, "Failed leash pathfind");
			return;
		}
	}
	else if(team->roamingLeash && team->roamingLeashPointValid)
	{
		Vec3 targetPos;

		aiGetSpawnPos(e, aib, targetPos);
		foundPath = aiMovementSetTargetPositionEx(e, aib, targetPos, &didPathfind, flags, func, file, line);
	}
	else
	{
		foundPath = aiMovementSetTargetPositionEx(e, aib, aib->spawnPos, &didPathfind,
			flags | AI_MOVEMENT_TARGET_IGNORE_CAPSULE_FOR_DIST, func, file, line);

		if(foundPath)
			aiMovementSetFinalFaceRot(e, aib, aib->spawnRot);
		else if(aib->spawnBeacon)
		{
			foundPath = aiMovementSetTargetPositionEx(e, aib, aib->spawnBeacon->pos, &didPathfind,
				flags | AI_MOVEMENT_TARGET_CRITICAL, func, file, line);

			if(foundPath)
				copyVec3(aib->spawnBeacon->pos, aib->spawnPos);
		}
	}

	if(!foundPath && didPathfind)
	{
		entSetPos(e, pos, 1, "Couldn't go to spawn pos");
	}
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GetOriginalSpawnPos);
void exprFuncGetOriginalSpawnPos(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_LOC_MAT4_OUT matOut)
{
	copyVec3(e->aibase->spawnPos, matOut[3]);
}

AUTO_EXPR_FUNC(ai) ACMD_NAME(GoToOriginalSpawnPos);
void exprFuncGoToOriginalSpawnPos(ACMD_EXPR_SELF Entity* e)
{
	AIVarsBase* aib = e->aibase;

	aiMovementSetTargetPosition(e, aib, aib->spawnPos, NULL,
		AI_MOVEMENT_TARGET_IGNORE_CAPSULE_FOR_DIST | AI_MOVEMENT_TARGET_CRITICAL);
	aiMovementSetFinalFaceRot(e, aib, aib->spawnRot);
}

void aiMovementUpdateConfigSettings(Entity* e, AIVarsBase* aib, AIConfig* config)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);

	fg->updated.config = 1;

	memcpy(&fg->config, &config->movementParams, sizeof(config->movementParams));
	mrEnableMsgCreateToBG(aib->movement);
}

void aiMovementUpdateSpawnOffset(Entity* e, AIVarsBase* aib)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	aib->spawnOffsetDirtied = 0;

	if(fg->orders.useOffset && !sameVec3(fg->orders.targetOffset, aib->spawnOffset))
	{
		copyVec3(aib->spawnOffset, fg->orders.targetOffset);

		fg->updated.orders = 1;
		mrEnableMsgCreateToBG(aib->movement);
	}
}

static void aiMovementEnableTeleport(Entity* e, AIVarsBase* aib, bool enabled)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	U32 tpDisabled = (U32)!enabled;
 
	if(fg->teleportDisabled!=tpDisabled)
	{
		fg->teleportDisabled = tpDisabled;
		
		fg->updated.teleportDisabled = 1;
		mrEnableMsgCreateToBG(aib->movement);
	}
}

void aiMovementHandleRegionChange(SA_PARAM_NN_VALID Entity* e, S32 prevRegion, S32 curRegion)
{
	if(curRegion==WRT_Space)
	{
		aiMovementEnableTeleport(e, e->aibase, false);
	}
}

void aiMovementGetSplineTarget(Entity *e, AIVarsBase *aib, Vec3 target)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);

	copyVec3(fg->splineTarget, target);
}

void aiMovementSetSleeping(SA_PARAM_NN_VALID Entity* be, SA_PARAM_NN_VALID AIVarsBase* aib, bool sleeping)
{
	AIMovementFG* fg = NULL;
	
	PERFINFO_AUTO_START_FUNC();

	fg = aiMovementGetFG(be, aib);
	
	sleeping = !!sleeping;
	
	if(fg->sleeping != (U32)sleeping){
		fg->sleeping = (U32)sleeping;
		mrEnableMsgCreateToBG(aib->movement);
		fg->updated.sleeping = 1;
	}

	PERFINFO_AUTO_STOP();
}

void aiMovementSetWalkRunDist(Entity* e, AIVarsBase* aib, F32 distWalk, F32 distRun, U32 cheat)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	if(distWalk || distRun)
	{
		MAX1(distWalk, 2);
		MAX1(distRun, distWalk+1);
	}

	if(fg->metaorders.distWalk!=distWalk || fg->metaorders.distRun!=distRun)
	{
		fg->metaorders.distWalk = distWalk;
		fg->metaorders.distRun = distRun;
		fg->metaorders.speedCheat = cheat;

		fg->updated.metaorders = 1;
		mrEnableMsgCreateToBG(aib->movement);
	}
}

AIMovementOrderType aiMovementGetMovementOrderType(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	return fg->orders.movementType;
}

AIMovementOrderEntDetail aiMovementGetMovementOrderEntDetail(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	return fg->orders.entDetail;
}

int aiMovementGetOrderUseOffset(SA_PARAM_NN_VALID Entity* e, SA_PARAM_NN_VALID AIVarsBase* aib)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);
	return fg->orders.useOffset;
}

EntityRef aiMovementGetMovementTargetEnt(Entity* e, AIVarsBase *aib)
{
	AIMovementFG *fg = aiMovementGetFG(e, aib);

	if(fg->orders.movementType==AI_MOVEMENT_ORDER_ENT)
		return fg->orders.targetRef;

	return 0;
}

EntityRef aiMovementGetRotationTargetEnt(Entity* e, AIVarsBase *aib)
{
	AIMovementFG *fg = aiMovementGetFG(e, aib);
	
	if(fg->rotation.type == AI_MOVEMENT_ROTATION_ENTREF)
		return fg->rotation.finalFaceEntRef;
	
	return 0;
}

void aiMovementFly(Entity *e, AIVarsBase *aib, int fly)
{
	int i;

	for(i=0; i<eaSize(&aib->powers->powInfos); i++)
	{
		AIPowerInfo *info = aib->powers->powInfos[i];

		if(info->isFlyPower)
		{
			if(!!info->power->bActive && !fly)
			{
				aiUsePower(e, aib, info, NULL, NULL, NULL, 0, NULL, false, false);
			}
			if(!info->power->bActive && fly)
			{
				aiUsePower(e, aib, info, NULL, NULL, NULL, 1, NULL, false, false);
			}
			break;
		}
	}	
}

void aiMovementSetFlying(Entity* be, AIVarsBase* aib, bool flying)
{
	AIMovementFG* fg = aiMovementGetFG(be, aib);
	
	flying = !!flying;
	
	if(fg->flying != (U32)flying){
		fg->flying = (U32)flying;
		mrEnableMsgCreateToBG(aib->movement);
		fg->updated.flying = 1;
	}

	if(be->pCritter && fg->flying && !aib->powers->hasFlightPowers && !aib->powers->alwaysFlight)
	{
		CritterDef *cdef = GET_REF(be->pCritter->critterDef);
		if(cdef)
		{
			static StashTable unawareFlyers = NULL;

			if(!unawareFlyers)
				unawareFlyers = stashTableCreateWithStringKeys(10, StashDefault);

			if(!stashFindInt(unawareFlyers, cdef->pchName, NULL))
			{
				ErrorFilenamef(cdef->pchFileName, 
								"Critter: %s is being set as flying but AI is unaware."
								"  Make sure his flight power has the AI flight tag.",	
								cdef->pchName);

				stashAddInt(unawareFlyers, cdef->pchName, 1, 1);
			}
		}
	}
}

int aiMovementGetAlwaysFlyEntRef(EntityRef ref)
{
	Entity *e = entFromEntityRefAnyPartition(ref);
	if(e)
	{
		AIVarsBase *aib = e->aibase;

		if(!aib)
			return 0;
		else
		{
			AIConfig *config = aiGetConfig(e, aib);

			return aib->powers && (aib->powers->alwaysFlight || aib->powers->hasFlightPowers && config && config->movementParams.alwaysFly);
		}
	}

	return 0;
}

int aiMovementGetNeverFlyEntRef(EntityRef ref)
{
	Entity *e = entFromEntityRefAnyPartition(ref);
	if(e)
	{
		AIVarsBase *aib = e->aibase;

		if(!aib)
			return 0;
		else
		{
			AIConfig *config = aiGetConfig(e, aib);

			return config->movementParams.neverFly;
		}
	}

	return 0;
}

int aiMovementGetCanFlyEntRef(EntityRef ref)
{
	Entity *e = entFromEntityRefAnyPartition(ref);
	if(e)
	{
		AIVarsBase *aib = e->aibase;

		return aib->powers && (aib->powers->alwaysFlight || aib->powers->hasFlightPowers);
	}

	return 0;
}

int aiMovementGetFlyingEntRef(EntityRef ref)
{
	Entity *e = entFromEntityRefAnyPartition(ref);
	if(e)
	{
		AIVarsBase *aib = e->aibase;

		return aib && aiMovementGetFlying(e, aib);
	}
	
	return 0;
}

F32 aiMovementGetTurnRate(Entity* e)
{
	F32 turnRate = 0;

	if(mrFlightGetTurnRate(e->mm.mrFlight, &turnRate))
		return turnRate;

	return 0;
}

F32 aiMovementGetTurnRateEntRef(EntityRef ref)
{
	Entity* e = entFromEntityRefAnyPartition(ref);

	if(e)
		return aiMovementGetTurnRate(e);
	
	return 0;
}

F32 aiMovementGetJumpHeight(Entity* e)
{
	F32 jumpHeight = 10;
	if(mrSurfaceGetJumpHeight(SAFE_MEMBER(e, mm.mrSurface), &jumpHeight))
		return jumpHeight;

	return jumpHeight;	
}

F32 aiMovementGetJumpHeightEntRef(EntityRef ref)
{
	Entity* e = entFromEntityRefAnyPartition(ref);

	if(e)
		return aiMovementGetJumpHeight(e);

	return 10;
}

F32 aiMovementGetJumpDistMultEntRef(EntityRef ref)
{
	Entity* e = entFromEntityRefAnyPartition(ref);
	AIVarsBase *aib;
	AIConfig* config;

	if(!e)
		return 2;

	aib = e->aibase;

	config = aiGetConfig(e, aib);

	return config->movementParams.jumpDistCostMult;
}

F32 aiMovementGetJumpHeightMultEntRef(EntityRef ref)
{
	Entity* e = entFromEntityRefAnyPartition(ref);
	AIVarsBase *aib;
	AIConfig* config;

	if(!e)
		return 2;

	aib = e->aibase;

	config = aiGetConfig(e, aib);

	return config->movementParams.jumpHeightCostMult;
}

F32 aiMovementGetJumpCostEntRef(EntityRef ref)
{
	Entity* e = entFromEntityRefAnyPartition(ref);
	AIVarsBase *aib;
	AIConfig* config;

	if(!e)
		return 2;

	aib = e->aibase;

	config = aiGetConfig(e, aib);

	return config->movementParams.jumpCostInFeet;
}

int aiMovementGetFlying(Entity* be, AIVarsBase* aib)
{
	AIMovementFG* fg = aiMovementGetFG(be, aib);
	return fg->flying;
}

void aiMovementAddHoldBit(Entity *e, U32 bitHandle)
{
	AIMovementFG* fg = aiMovementGetFG(e, e->aibase);

	ea32PushUnique(&fg->animHold, bitHandle);

	fg->updated.animHold = 1;
	mrEnableMsgCreateToBG(e->aibase->movement);
}

void aiMovementClearAnimHold(Entity *e)
{
	AIMovementFG* fg = aiMovementGetFG(e, e->aibase);

	if(ea32Size(&fg->animHold))
	{
		ea32ClearFast(&fg->animHold);
		fg->updated.animHold = 1;
		mrEnableMsgCreateToBG(e->aibase->movement);
	}
}

void aiMovementAddAnimBitHandle(Entity* be, U32 bitHandle, int* handleOut)
{
	AIMovementFG* fg = aiMovementGetFG(be, be->aibase);

	AIMovementAnimFXHandle* handle = aiMovementAnimFXHandleCreate();
	
	handle->bitHandle = bitHandle;
	handle->aiMovementHandle = aiMovementGetNewId();

	*handleOut = handle->aiMovementHandle;

	eaPush(&fg->animFxAdd, handle);

	mrEnableMsgCreateToBG(be->aibase->movement);
	fg->updated.animBits = 1;
}

void aiMovementAddFX(Entity* be, const char* name, int* handleOut)
{
	AIMovementFG* fg = aiMovementGetFG(be, be->aibase);
	
	AIMovementAnimFXHandle* handle = aiMovementAnimFXHandleCreate();

	handle->name = allocAddString(name);
	handle->aiMovementHandle = aiMovementGetNewId();

	*handleOut = handle->aiMovementHandle;

	eaPush(&fg->animFxAdd, handle);
	mrEnableMsgCreateToBG(be->aibase->movement);
	fg->updated.animBits = 1;
}

void aiMovementFlashFX(Entity* e, const char* name)
{
	AIMovementFG* fg = aiMovementGetFG(e, e->aibase);
	AIMovementAnimFXHandle* handle = aiMovementAnimFXHandleCreate();

	handle->name = allocAddString(name);
	handle->aiMovementHandle = aiMovementGetNewId();
	handle->destroyAfterOneTick = 1;
	
	eaPush(&fg->animFxAdd, handle);
	mrEnableMsgCreateToBG(e->aibase->movement);
	fg->updated.animBits = 1;
}

void aiMovementRemoveAnimBitHandle(Entity* be, int aiMovementHandle)
{
	AIMovementFG* fg = aiMovementGetFG(be, be->aibase);

	AIMovementAnimFXHandle* handle = aiMovementAnimFXHandleCreate();

	handle->aiMovementHandle = aiMovementHandle;

	eaPush(&fg->animFxCancel, handle);
	mrEnableMsgCreateToBG(be->aibase->movement);
	fg->updated.animBits = 1;
}

void aiMovementRemoveFX(Entity* be, const char* name, int aiMovementHandle)
{
	AIMovementFG* fg = aiMovementGetFG(be, be->aibase);

	AIMovementAnimFXHandle* handle = aiMovementAnimFXHandleCreate();

	handle->name = allocAddString(name);
	handle->aiMovementHandle = aiMovementHandle;

	eaPush(&fg->animFxCancel, handle);
	mrEnableMsgCreateToBG(be->aibase->movement);
	fg->updated.animBits = 1;
}

void aiMovementSetRotationFlag(Entity* be, U32 disabled)
{
	AIMovementFG* fg = aiMovementGetFG(be, be->aibase);

	if(fg->rotationDisabled != disabled)
	{
		fg->rotationDisabled = disabled;
		fg->updated.rotationDisabled = 1;
		mrEnableMsgCreateToBG(be->aibase->movement);		
	}
}

void aiMovementSetDebugFlag(Entity *e, bool on)
{
	AIVarsBase *aib = e->aibase;
	AIMovementFG *fg = aiMovementGetFG(e, aib);

	aib->debugPath = !!on;

	if(fg->debugForceProcess!=(U32)!!on)
	{
		fg->debugForceProcess = !!on;
		fg->updated.debugForceProcess = 1;
	}

	mrEnableMsgCreateToBG(aib->movement);
}

void aiMovementAvoidEntryAdd(Entity* e, AIVarsBase* aib, AIVolumeEntry* entry)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);

#ifdef AI_PARANOID_AVOID
	int i;

	for(i = eaSize(&fg->avoidAdd)-1; i >= 0; i--)
		devassert(fg->avoidAdd[i] != entry);

	devassert(!entry->addTimeStamp);
	entry->addTimeStamp = ABS_TIME;
	ASSERT_FALSE_AND_SET(entry->sentToBG);
	eaPush(&fg->adds, entry);
#endif

	eaPush(&fg->avoidAdd, entry);
	mrEnableMsgCreateToBG(aib->movement);
	fg->updated.avoid = 1;
}

void aiMovementAvoidEntryRemove(Entity* e, AIVarsBase* aib, AIVolumeEntry* entry)
{
	AIMovementFG* fg = aiMovementGetFG(e, aib);

#ifdef AI_PARANOID_AVOID
	int i;

	for(i = eaSize(&fg->avoidRemove)-1; i >= 0; i--)
		devassert(fg->avoidRemove[i] != entry);

	devassert(entry->addTimeStamp && entry->addTimeStamp <= ABS_TIME);
	devassert(!entry->remTimeStamp);
	entry->remTimeStamp = ABS_TIME;
	ASSERT_TRUE_AND_RESET(entry->sentToBG);
	eaPush(&fg->removes, entry);
#endif

	eaPush(&fg->avoidRemove, entry);
	mrEnableMsgCreateToBG(aib->movement);
	fg->updated.avoid = 1;
}

void aiMovementSetOverrideSpeed(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase* aib, F32 fSpeedOverride)
{
	if (aib->hConfigSpeedOverride)
	{
		aiConfigModRemove(e, aib, aib->hConfigSpeedOverride);
		aib->hConfigSpeedOverride = 0;
	}
	
	{
		char* pszSpeedStr = NULL;
		estrStackCreate(&pszSpeedStr);
		estrPrintf(&pszSpeedStr, "%f", fSpeedOverride);
		aib->hConfigSpeedOverride = aiConfigModAddFromString(e, aib, "overrideMovementSpeed", pszSpeedStr, NULL);
		estrDestroy(&pszSpeedStr);
	}
}

void aiMovementClearOverrideSpeed(SA_PARAM_NN_VALID Entity *e, SA_PARAM_NN_VALID AIVarsBase* aib)
{
	if (aib->hConfigSpeedOverride)
	{
		aiConfigModRemove(e, aib, aib->hConfigSpeedOverride);
		aib->hConfigSpeedOverride = 0;
	}
}

bool getMyTrackerDataTypeAndColumn(ParseTable pti[], int* type)
{
	int inType, typecol;

	typecol = ParserGetTableObjectTypeColumn(pti);

	if(!devassertmsg(typecol != -1, "Trying to get data for an invalid parsetable (not a polymorphic struct)"))
		return false;

	inType = pti[typecol].param; // get default value, which AW assures me is the polymorphic type

	if(type)
		*type = inType;

	return true;
}

void* addMyData(ExprLocalData ***localData, ParseTable pti[], U64 key)
{
	if(!getMyTrackerDataTypeAndColumn(pti, NULL))
		return NULL;

	if(localData)
	{
		ExprLocalData* data = StructCreateVoid(pti);

		data->key = key;

		eaPush(localData, data);

		return data;
	}

	return NULL;
}

void* getMyDataInternal(ExprLocalData*** localData, ParseTable pti[], U64 key, int create)
{
	int i, n;
	int type;

	if(!getMyTrackerDataTypeAndColumn(pti, &type))
		devassertmsgf(0, "Can't find FSM type information for ParseTable: %s", ParserGetTableName(pti));

	if(localData)
	{
		n = eaSize(localData);
		for(i = 0; i < n; i++)
		{
			ExprLocalData* curData = (*localData)[i];
			if(curData->type == type && (!key || curData->key == key))
				return curData;
		}
	}

	return create ? addMyData(localData, pti, key) : NULL;
}

void* getMyDataFromTracker(ExprContext* context, ParseTable pti[], U64 key, int create)
{
	ExprLocalData*** localData = NULL;

	exprContextGetCleanupCommandQueue(context, NULL, &localData);

	return getMyDataInternal(localData, pti, key, create);
}

void* getMyData(ExprContext* context, ParseTable pti[], U64 key)
{
	return getMyDataFromTracker(context, pti, key, true);
}

void* getMyDataIfExists(ExprContext* context, ParseTable pti[], U64 key)
{
	return getMyDataFromTracker(context, pti, key, false);
}

void* getMyDataFromData(ExprLocalData ***localData, ParseTable pti[], U64 key)
{
	return getMyDataInternal(localData, pti, key, true);
}

void* getMyDataFromDataIfExists(ExprLocalData ***localData, ParseTable pti[], U64 key)
{
	return getMyDataInternal(localData, pti, key, false);
}

AUTO_COMMAND_QUEUED();
void ExecuteQueue(ACMD_POINTER CommandQueue* queue)
{
	if(queue)
		CommandQueue_ExecuteAllCommands(queue);
}

AUTO_COMMAND_QUEUED();
void deleteMyData(ACMD_POINTER ExprContext* context, ACMD_POINTER ParseTable* pti, ACMD_POINTER ExprLocalData ***localData, U64 key)
{
	int i, n;
	int type;

	if(!getMyTrackerDataTypeAndColumn(pti, &type))
		return;

	n = eaSize(localData);
	for(i = 0; i < n; i++)
	{
		ExprLocalData* curData = (*localData)[i];
		if(curData->type == type && (!key || curData->key == key))
		{
			StructDestroyVoid(pti, (*localData)[i]);
			eaRemoveFast(localData, i);
			return;
		}
	}

	//devassertmsg(0, "Was told to delete data, but I can't find it... Wrong parse table specified?");
}

#include "oldencounter_common.h"

static int patrolDebugText = false;
AUTO_CMD_INT(patrolDebugText, patrolDebugText);

AUTO_COMMAND_QUEUED();
void efPatrolWaypointPassed(EntityRef ref, ACMD_POINTER ExprContext* context, ACMD_POINTER NavPathWaypoint* wp, int point)
{
	FSMLDPatrol* mydata;
	Entity* e = entFromEntityRefAnyPartition(ref);
	AIVarsBase* aib;
	AITeam* team;
	FSMContext *fsmContext;

	if(!e)
		return;

	aib = e->aibase;
	team = aiTeamGetAmbientTeam(e, aib);

	fsmContext = exprContextGetFSMContext(context);
	if(!SAFE_MEMBER(fsmContext, curTracker))
		return;

	exprContextCleanupPush(context, NULL, &fsmContext->curTracker->localData);

	mydata = getMyDataIfExists(context, parse_FSMLDPatrol, 0);

	if(!mydata)
	{
		exprContextCleanupPop(context);
		return;
	}

	if(team && e==aiTeamGetLeader(team) && mydata->useOffset)
	{
		int i;
		for(i=eaSize(&team->members)-1; i>=0; i--)
		{
			Entity* teamE = team->members[i]->memberBE;
			FSMLDPatrol* teamEData;

			if(teamE==e)
				continue;

			fsmContext = exprContextGetFSMContext(teamE->aibase->exprContext);
			if(!SAFE_MEMBER(fsmContext, curTracker))
				continue;

			exprContextCleanupPush(teamE->aibase->exprContext, NULL, &fsmContext->curTracker->localData);

			teamEData = getMyDataIfExists(teamE->aibase->exprContext, parse_FSMLDPatrol, 0);

			if(teamEData && teamEData->useOffset)
				efPatrolWaypointPassed(entGetRef(teamE), teamE->aibase->exprContext, wp, point);

			exprContextCleanupPop(teamE->aibase->exprContext);
		}
	}

	mydata->finishedOneRotation |= (U32)point == mydata->totalPoints - 1;
	mydata->pingpongRev = point && point < mydata->lastPointPassed;
	mydata->lastPointPassed = point;

	AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 4, "Patrol - Reached %d "LOC_PRINTF_STR, point, vecParamsXYZ(wp->pos));

	if(patrolDebugText)
	{
		printf("%s (%d) passed point %d (%.1f, %.1f, %.1f), has %sfinished one rotation and is %sgoing in reverse pingpong dir\n",
			e->debugName, e->myRef, point, vecParamsXYZ(wp->pos),
			mydata->finishedOneRotation ? "" : "not ",
			mydata->pingpongRev ? "" : "not ");
	}

	exprContextCleanupPop(context);
}

AUTO_COMMAND_QUEUED();
void efPatrolUnsetDataBit(ACMD_POINTER ExprContext* context)
{
	FSMLDPatrol* mydata = getMyDataIfExists(context, parse_FSMLDPatrol, 0);

	if(mydata)
		mydata->dataSet = 0;
}

// Returns whether the current entity has finished one cycle of its path for its
// current patrol (this state)
AUTO_EXPR_FUNC(ai) ACMD_NAME(PatrolCompletedOneIteration);
int exprFuncPatrolCompletedOneIteration(ExprContext* context)
{
	FSMLDPatrol* mydata = getMyData(context, parse_FSMLDPatrol, 0);

	return mydata->finishedOneRotation;
}

// Resets this entity's patrol for this state so it starts from the beginning
AUTO_EXPR_FUNC(ai) ACMD_NAME(PatrolReset);
void exprFuncPatrolReset(ExprContext* context)
{
	FSMLDPatrol* mydata = getMyDataIfExists(context, parse_FSMLDPatrol, 0);
	FSMStateTrackerEntry* tracker = exprContextGetCurStateTracker(context);

	if(!mydata)
		return;

	if(!getMyTrackerDataTypeAndColumn(parse_FSMLDPatrol, NULL))
		return;

	deleteMyData(context, parse_FSMLDPatrol, &tracker->localData, 0);
}

ExprFuncReturnVal aiMovementDoPatrol(ACMD_EXPR_SELF Entity* e, ExprContext* context, const char* patrolName, int useOffset, int useNearest, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDPatrol* mydata = getMyData(context, parse_FSMLDPatrol, (U64)patrolName);
	CommandQueue* exitHandlers = NULL;
	int skipData = 0;
	AIVarsBase* aib = e->aibase;
	AITeam *team = aiTeamGetAmbientTeam(e, aib);
	int iPartitionIdx = entGetPartitionIdx(e);
	
	exprContextGetCleanupCommandQueue(context, &exitHandlers, NULL);

	if(mydata->leaderRef)
	{
		Entity *leader = entFromEntityRef(iPartitionIdx, mydata->leaderRef);

		if(leader!=aiTeamGetLeader(team))
		{
			// If dataSet, then the leader died WHILE patrolling instead of during another state,
			// so we do not want to re-add the side effects of configmod and exithandlers
			skipData = mydata->dataSet;
			mydata->dataSet = 0;
		}
	}

	if(!mydata->dataSet)
	{
		AIMovementFG* fg = aiMovementGetFG(e, aib);
		WorldScope *scope = NULL;
		GamePatrolRoute* route = NULL;
		WorldPatrolRouteType routeType = PATROL_PINGPONG;
		int i, numpoints;
		int keepWaypoints;
		NavPath* path = &fg->pathNormal;
		Vec3 myOffset;
		EntityRef myRef = entGetRef(e);
		ExprFuncReturnVal funcretval;
		Entity *leader = aiTeamGetLeader(team);

		if(e->pCritter && 
			e->pCritter->encounterData.pGameEncounter && 
			e->pCritter->encounterData.pGameEncounter->pWorldEncounter)
		{
			scope = e->pCritter->encounterData.pGameEncounter->pWorldEncounter->common_data.closest_scope;
		}

		route = patrolroute_GetByName(patrolName, scope);

		if(leader && leader!=e && useOffset && !team->offsetPatrolName)
		{
			FSMStateTrackerEntry *entry = exprContextGetCurStateTracker(context);
			if(ABS_TIME_PASSED_PARTITION(iPartitionIdx, entry->lastEntryTime,5))
			{
				estrPrintf(errString, "Critter: %s | Leader: %s | Timeout while waiting for leader to begin offsetpatrol.  Make sure leader is also doing patrol.", ENTDEBUGNAME(e), ENTDEBUGNAME(leader));
				return ExprFuncReturnError;
			}
			return ExprFuncReturnFinished;
		}

		AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 3, "Patrol - %s patrol %s", mydata->hadData ? "Resuming" : "Starting", patrolName)

		mydata->dataSet = 1;
		mydata->useOffset = useOffset;

		if(!skipData)
		{
			funcretval = exprFuncAddConfigMod(e, context, "RoamingLeash", "1", errString);
			if(funcretval != ExprFuncReturnFinished)
				return funcretval;
		}

		if(useNearest)
		{
			funcretval = exprFuncAddConfigMod(e, context, "SkipLeashing", "1", errString);
			if(funcretval != ExprFuncReturnFinished)
				return funcretval;
		}

		if(!exitHandlers)
		{
			estrPrintf(errString, "Unable to call patrol in this section - missing exit handlers");
			return ExprFuncReturnError;
		}

		funcretval = exprFuncAddConfigMod(e, context, "RoamingLeash", "1", errString);
		if(funcretval != ExprFuncReturnFinished)
			return funcretval;

		if(!route)
		{
			char* errorstr = NULL;
			const char* filename = NULL;
			estrStackCreate(&errorstr);

			// TODO: exprusageerror
			estrPrintf(&errorstr, "Specified named route %s could not be found", patrolName);
			if(e->pCritter)
			{
				if (e->pCritter->encounterData.pGameEncounter)
				{
					GameEncounter *pEncounter = e->pCritter->encounterData.pGameEncounter;
					const char *pcActorName = encounter_GetActorName(pEncounter, e->pCritter->encounterData.iActorIndex);
					filename = encounter_GetFilename(pEncounter);

					estrConcatf(&errorstr, " on Actor %s of Static Encounter %s", pcActorName, filename);
				}
				else if (gConf.bAllowOldEncounterData && e->pCritter->encounterData.sourceActor && e->pCritter->encounterData.parentEncounter)
				{
					OldStaticEncounter* statEnc = GET_REF(e->pCritter->encounterData.parentEncounter->staticEnc);
					char* actorStr = NULL;

					if(statEnc)
						filename = statEnc->pchFilename;

					estrStackCreate(&actorStr);

					oldencounter_GetActorName(e->pCritter->encounterData.sourceActor, &actorStr);
					estrConcatf(&errorstr, " on Actor %s of Static Encounter %s", actorStr, 
						statEnc ? statEnc->name : "N/A");

					estrDestroy(&actorStr);
				}
				else if(e->debugName && e->debugName[0])
					estrConcatf(&errorstr, " on %s", e->debugName);
			}
			ErrorFilenamef(filename, "%s", errorstr);
			estrDestroy(&errorstr);
			return ExprFuncReturnFinished;
		}

		routeType = patrolroute_GetType(route);
		if(mydata->hadData && mydata->finishedOneRotation &&
			routeType != PATROL_PINGPONG &&
			routeType != PATROL_CIRCLE)
		{
			AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 3, "Patrol - skipping because completed one iter");
			return ExprFuncReturnFinished;
		}

		numpoints = patrolroute_GetNumPoints(route);

		if(!numpoints)
		{
			estrPrintf(errString, "Patrol route does not have waypoints");
			return ExprFuncReturnError;
		}

		mydata->totalPoints = numpoints;

		if(!skipData)
		{
			QueuedCommand_aiMovementResetPath(exitHandlers, e, aib);
			QueuedCommand_efPatrolUnsetDataBit(exitHandlers, context);
		}

		if(leader && leader==e && mydata->useOffset)
		{
			if(!team->offsetPatrolName || stricmp(team->offsetPatrolName, route->pcName))
			{
				if(team->offsetPatrolName)
					free(team->offsetPatrolName);
				team->offsetPatrolName = strdup(route->pcName);
			}
		}
		else if(leader && mydata->useOffset)
		{
			if(team->offsetPatrolName && stricmp(team->offsetPatrolName, route->pcName))
				estrPrintf(errString, "Critter %s is running PatrolOffset %s while the leader is running %s", ENTDEBUGNAME(e), team->offsetPatrolName, route->pcName);
		}

		if (leader && mydata->useOffset && !team->pTeamFormation)
		{
			aiFormation_CreateFormationForPatrol(team);
			aiFormation_UpdateFormation(entGetPartitionIdx(e), team->pTeamFormation);
		}

		mydata->leaderRef = 0;
		if(leader && leader!=e && mydata->useOffset)
		{
			mydata->hadData = 1; // this lets patrol know if it's resuming or not
			mydata->leaderRef = entGetRef(leader);
			mydata->patrolName = patrolName;

			aiFormation_DoFormationMovementForMember(iPartitionIdx, team, e);

			AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 3, "Patrol - Letting leader do movement");

			return ExprFuncReturnFinished;
		}

		navPathClear(&fg->pathNormal);

		if(numpoints == 1 && (routeType == PATROL_CIRCLE || routeType == PATROL_PINGPONG))
		{
			estrPrintf(errString, "Patrol route %s is not allowed to be circle or pingpong and only have one waypoint", patrolName);
			return ExprFuncReturnError;
		}

		path->pingpong = routeType == PATROL_PINGPONG;
		path->circular = routeType == PATROL_CIRCLE;

		keepWaypoints = path->pingpong || path->circular;

		if(mydata->lastPointPassed >= 0 && mydata->lastPointPassed < numpoints && 
			mydata->patrolName==patrolName)
		{
			if (useNearest)
			{ 
				int bestDistSqIdx = -1; // find nearest waypoint to continue from
				F32 bestDistSq = 0.0f;
				for (i=0;i<numpoints;i++)
				{
					Vec3 vLoc;
					if (patrolroute_GetPointLocation(route,i,vLoc))
					{
						F32 distSq;
						Vec3 vEnt;
						entGetPos(e,vEnt);
						distSq = distance3Squared(vLoc,vEnt);
						if (bestDistSqIdx == -1 || distSq < bestDistSq)
						{
							bestDistSqIdx = i;
							bestDistSq = distSq;
						}
					}
				}
				if (bestDistSqIdx >= 0)
					path->curWaypoint = bestDistSqIdx;
				else
					path->curWaypoint = mydata->lastPointPassed;
			} 
			else
			{
				path->curWaypoint = mydata->lastPointPassed; // continue where we left off
			}

			AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 3, "Patrol - Resuming at %d%s", path->curWaypoint, useNearest ? " (nearest)" : "");
		}

		path->pingpongRev = mydata->pingpongRev;

		if(team->calcOffsetOnDemand)
		{
			int count = 0;
			Vec3 center = {0};
			Vec3 pos;

			for(i=eaSize(&team->members)-1; i>=0; i--)
			{
				AITeamMember *member = team->members[i];

				if(!entIsPlayer(member->memberBE))
				{
					entGetPos(member->memberBE, pos);

					addVec3(center, pos, center);
					count++;
				}
			}
			if(!count)
			{
				estrPrintf(errString, "Trying to patrol when only players are on a team?");
				return ExprFuncReturnError;
			}
			entGetPos(e, pos);
			scaleVec3(center, 1.0/count, center);
			subVec3(pos, center, myOffset);
		}
		else
			subVec3(aib->spawnPos, team->spawnPos, myOffset);

		for(i = 0; i < numpoints; i++)
		{
			NavPathWaypoint* wp = createNavPathWaypoint();
			//printf("(%d,%x)", i, wp);
			wp->connectType = NAVPATH_CONNECT_ATTEMPT_SHORTCUT;
			patrolroute_GetPointLocation(route, i, wp->pos);
			if(useOffset)
			{
				Vec3 tmpGrnd;
				addVec3(wp->pos, myOffset, wp->pos);
				if(aiFindGroundPosition(worldGetActiveColl(entGetPartitionIdx(e)), wp->pos, tmpGrnd))
				{
					copyVec3(tmpGrnd, wp->pos);
					wp->pos[1] += 2.0;
				}
			}
			//vecY(wp->pos) += 2.0;  		// These positions are already offset from the ground
			navPathAddTail(path, wp);
			wp->keepWaypoint = keepWaypoints;
			wp->commandsWhenPassed = CommandQueue_Create(8 * sizeof(void*), false);
			QueuedCommand_efPatrolWaypointPassed(wp->commandsWhenPassed, myRef, context, wp, i);

			if(globalWPParanoia)
				assert(wp->dts.dts==DTS_NONE);
			dtsSetState(&wp->dts, DTS_INFG);

			AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 3, "Patrol - Point %d, Pos "LOC_PRINTF_STR, 
									i, vecParamsXYZ(wp->pos));
		}

		// if you're just starting your patrol (or it's been reset), you
		// want to actually start at point 0, not 1
		if(mydata->hadData && mydata->lastPointPassed != -1)
		{
			navPathUpdateNextWaypoint(path);
			if (path->curWaypoint == -1)
			{	// we went off the end of the path, start over.
				AI_DEBUG_PRINT(e, AI_LOG_MOVEMENT, 3, "Patrol - Restarting at %d", path->curWaypoint);
				path->curWaypoint = 0;
				mydata->lastPointPassed = -1;
			}
		}
		else
			mydata->lastPointPassed = -1;

		aiMovementUpdateBGNavSearch(e, aib);
		mydata->patrolName = patrolName;

		mydata->hadData = 1; // this lets patrol know if it's resuming or not
	}
	else if(mydata->useOffset)
	{
		aiFormation_DoFormationMovementForMember(iPartitionIdx, team, e);
	}
	

	return ExprFuncReturnFinished;
}

// Runs the patrol path specified by <patrolName>, offset by the entity's relative position, resumes at nearest waypoint
// in its encounter
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(PatrolResumeNearestNamedOffset) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncPatrolResumeNearestNamedOffset(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_SC_TYPE("PatrolRoute") const char* patrolName, ACMD_EXPR_ERRSTRING errString)
{
	return aiMovementDoPatrol(be, context, patrolName, true, true, errString);
}

// Runs the patrol path specified by <patrolName>, resumes at nearest waypoint
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(PatrolResumeNearestNamed) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncPatrolResumeNearestNamed(ACMD_EXPR_SELF Entity* be, ExprContext* context, const char* patrolName, ACMD_EXPR_ERRSTRING errString)
{
	return aiMovementDoPatrol(be, context, patrolName, false, true, errString);
}

// Runs the patrol path specified by <patrolName>, offset by the entity's relative position
// in its encounter
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(PatrolNamedOffset) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncPatrolNamedOffset(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_SC_TYPE("PatrolRoute") const char* patrolName, ACMD_EXPR_ERRSTRING errString)
{
	return aiMovementDoPatrol(be, context, patrolName, true, false, errString);
}

// Runs the patrol path specified by <patrolName>
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(PatrolNamed) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncPatrolNamed(ACMD_EXPR_SELF Entity* be, ExprContext* context, const char* patrolName, ACMD_EXPR_ERRSTRING errString)
{
	return aiMovementDoPatrol(be, context, patrolName, false, false, errString);
}

// Tells the AI when to run and walk, so it can approach the target leisurely (walk=30% max speed)
//   The ai will cheat and move 20% faster than max speed when it is 20% farther away than dist run
AUTO_EXPR_FUNC(ai) ACMD_NAME(SetSpeedDist);
ExprFuncReturnVal exprFuncSetSpeedDist(ACMD_EXPR_SELF Entity* e, F32 distWalk, F32 distRun, U32 cheat)
{
	AIVarsBase *aib = e->aibase;

	aiMovementSetWalkRunDist(e, aib, distWalk, distRun, cheat);

	return ExprFuncReturnFinished;
}

AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(MovementResetPath) ACMD_EXPR_FUNC_COST_MOVEMENT;
void exprFuncMovementResetPath(ACMD_EXPR_SELF Entity* e)
{
	if (e->aibase)
	{
		aiMovementResetPath(e, e->aibase);
	}
}

// Follows the passed in entity with the entity's formation offset
//  This is mostly used for pets - most other cases will generally use patroloffset or runtopointoffset
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(FollowOffset) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncFollowOffset(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ENTARRAY_IN followEnts,
										ACMD_EXPR_ERRSTRING_STATIC errString)
{
	FSMLDFollow* mydata = getMyData(context, parse_FSMLDFollow, 0);

	if(!eaSize(followEnts))
	{
		//ErrorFilenamef(context->curExpr->filename, "No entities specified to follow!");
		return ExprFuncReturnFinished;
	}

	if(eaSize(followEnts) > 1)
	{
		*errString = "Can only follow one entity at a time, multiple were passed in";
		return ExprFuncReturnError;
	}

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetCurStateTracker(context);
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;
		EntityRef entRef = aiMovementGetMovementTargetEnt(be, be->aibase);

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers)
		{
			*errString = "Unable to call follow in this section - missing exit handlers";
			return ExprFuncReturnError;
		}

		QueuedCommand_aiMovementResetPath(exitHandlers, be, be->aibase);
		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDFollow, localData, 0);
		mydata->addedExitHandlers = 1;

		exprFuncAddConfigMod(be, context, "RoamingLeash", "1", errString);

		entRef = aiMovementGetMovementTargetEnt(be, be->aibase);
		if (entRef != entGetRef((*followEnts)[0]))
		{
			if (be->aibase->pFormationData)
			{
				// Force the formation to resettle
				be->aibase->pFormationData->pFormation->bIsDirty = true;
			}

			aiMovementSetTargetEntity(be, be->aibase, (*followEnts)[0], be->aibase->spawnOffset, 1, AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET, AI_MOVEMENT_TARGET_CRITICAL);
		}
	}
	else
	{
		// check to see if we're still being told to follow the ent, if not, reset the aiMovementSetTargetEntity 
		EntityRef entRef = aiMovementGetMovementTargetEnt(be, be->aibase);
		if (entRef != entGetRef((*followEnts)[0]))
		{	
			aiMovementSetTargetEntity(be, be->aibase, (*followEnts)[0], be->aibase->spawnOffset, 1, AI_MOVEMENT_ORDER_ENT_FOLLOW_OFFSET, AI_MOVEMENT_TARGET_CRITICAL);
		}
	}

	return ExprFuncReturnFinished;
}

// Follows the passed in entity
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(Follow) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncFollow(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ENTARRAY_IN followEnts,
					ACMD_EXPR_ERRSTRING_STATIC errString)
{
	FSMLDFollow* mydata = getMyData(context, parse_FSMLDFollow, 0);

	if(!mydata)
	{
		*errString = "Unable to call follow in this section - missing exit handlers";
		return ExprFuncReturnError;
	}

	if(!eaSize(followEnts))
	{
		//ErrorFilenamef(context->curExpr->filename, "No entities specified to follow!");
		return ExprFuncReturnFinished;
	}

	if(eaSize(followEnts) > 1)
	{
		*errString = "Can only follow one entity at a time, multiple were passed in";
		return ExprFuncReturnError;
	}

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetCurStateTracker(context);
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;
		
		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers)
		{
			*errString = "Unable to call follow in this section - missing exit handlers";
			return ExprFuncReturnError;
		}

		QueuedCommand_aiMovementResetPath(exitHandlers, be, be->aibase);
		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDFollow, localData, 0);
		mydata->addedExitHandlers = 1;

		exprFuncAddConfigMod(be, context, "RoamingLeash", "1", errString);
		
	}
	
	aiMovementSetTargetEntity(be, be->aibase, (*followEnts)[0], 
								NULL, false, AI_MOVEMENT_ORDER_ENT_FOLLOW, 
								AI_MOVEMENT_TARGET_CRITICAL);
	

	return ExprFuncReturnFinished;
}

// Turns movement system rotation disabling on or off
AUTO_EXPR_FUNC(ai) ACMD_NAME(NoRotation);
void exprFuncNoRotation(ACMD_EXPR_SELF Entity *be, ExprContext* context, int off)
{
	MovementRequester *mr = NULL;
	mmRequesterGetByNameFG(be->mm.movement, "AIMovement", &mr);

	if(!mr)
	{
		return;
	}

	aiMovementSetRotationFlag(be, !!off);
}

// Tells the critter to face a certain point (only when not moving)
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(FacePoint);
void exprFuncFacePoint(ACMD_EXPR_SELF Entity* e, ACMD_EXPR_LOC_MAT4_IN pointIn)
{
	aiMovementSetFinalFacePos(e, e->aibase, pointIn[3]);
}

// Tells the critter to face a certain entity (only when not moving)
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(FaceEntity);
ExprFuncReturnVal exprFuncFaceEntity(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ENTARRAY_IN entsIn, ACMD_EXPR_ERRSTRING errString)
{
	FSMLDGenericSetData* mydata;

	if(!eaSize(entsIn))
		return ExprFuncReturnFinished;

	if(!eaSize(entsIn) == 1)
	{
		estrPrintf(errString, "FaceEntity only supports facing one entity, %d passed in", eaSize(entsIn));
		return ExprFuncReturnError;
	}

	mydata = getMyData(context, parse_FSMLDGenericSetData, PTR_TO_UINT("FaceEntity"));
	
	if(!mydata->setData)
	{
		FSMStateTrackerEntry* tracker = exprContextGetFSMContext(context)->curTracker;

		if(!tracker->exitHandlers)
			tracker->exitHandlers = CommandQueue_Create(8, false);

		QueuedCommand_aiMovementResetPath(tracker->exitHandlers, e, e->aibase);
		QueuedCommand_deleteMyData(tracker->exitHandlers, context, parse_FSMLDGenericSetData, &tracker->localData, PTR_TO_UINT("FaceEntity"));
		mydata->setData = 1;
	}
	
	aiMovementSetFinalFaceEntity(e, e->aibase, (*entsIn)[0]);
	return ExprFuncReturnFinished;
}

// -------------------------------------------------------------------------------------------------------
// Wandering
// -------------------------------------------------------------------------------------------------------

typedef struct BeaconScoreData {
	Entity* e;
	F32 maxDistanceSQR;
	Vec3 maxDistanceAnchorPos;
	Vec3 facingDirection;
	F32 maxHeight;
	F32 minHeight;

	U32 groundRelativeHeights : 1;
} BeaconScoreData;

static int beaconScoreWander(Beacon* src, BeaconConnection* conn, BeaconScoreData *data, F32 *unused1, F32 *unused2){
	float* pos;
	if(conn){
		pos = conn->destBeacon->pos;
	}else{
		pos = data->maxDistanceAnchorPos;
	}


	if(aiShouldAvoidBeacon(entGetRef(data->e), src, data->minHeight))
		return 0;
	if(conn)
	{
		if(aiShouldAvoidBeacon(entGetRef(data->e), conn->destBeacon, data->minHeight))
			return 0;
		if(aiShouldAvoidLine(entGetRef(data->e), src, NULL, conn->destBeacon, data->minHeight, NULL))
			return 0;
	}

	if(data->maxDistanceSQR)
	{
		F32 distanceSQR = distance3Squared(data->maxDistanceAnchorPos, pos);
		if(distanceSQR > data->maxDistanceSQR)
			return 0;
	}

	if(conn)
	{
		F32 jumpHeight;
		Vec3 vBeaconConnectDir;
		subVec3(conn->destBeacon->pos, src->pos, vBeaconConnectDir);
		jumpHeight =  aiMovementGetJumpHeight(data->e);
		if (fabs(vBeaconConnectDir[1]) > jumpHeight) // don't follow this beacon if vertical too large
			return 0;
		return dotVec3(vBeaconConnectDir, data->facingDirection) > 0 ? rand()%20 : rand()%10;
	}
	
	return rand()%20;
}

// -------------------------------------------------------------------------------------------------------
static int beaconScoreWanderAir(Beacon* src, BeaconConnection* conn, BeaconScoreData *data, 
								F32 *minHeightOut, F32 *maxHeightOut){
	float* pos;

	if(conn){
		pos = conn->destBeacon->pos;
	}else{
		pos = src->pos;
	}

	if(aiShouldAvoidBeacon(entGetRef(data->e), src, data->minHeight))
		return 0;
	if(conn)
	{
		if(aiShouldAvoidBeacon(entGetRef(data->e), conn->destBeacon, data->minHeight))
			return 0;
		if(aiShouldAvoidLine(entGetRef(data->e), src, NULL, conn->destBeacon, data->minHeight, NULL))
			return 0;
	}

	// Ignore connections outside my desired heights
	if(conn)
	{
		if(data->groundRelativeHeights)
		{
			if(conn->maxHeight<data->minHeight || conn->minHeight>data->maxHeight)
				return 0;
		}
		else
		{
			if(pos[1]+conn->maxHeight<data->maxDistanceAnchorPos[1]-data->minHeight ||
					pos[1]+conn->minHeight>data->maxDistanceAnchorPos[1]+data->maxHeight)
				return 0;
		}
	}

	// Ignore beacons too far away and just ignore Y dist
	if(data->maxDistanceSQR && distance3SquaredXZ(data->maxDistanceAnchorPos, pos)>data->maxDistanceSQR){
		return 0;
	}

	if(minHeightOut && maxHeightOut)
	{
		if(data->groundRelativeHeights)
		{
			*minHeightOut = data->minHeight;
			*maxHeightOut = data->maxHeight;
			*minHeightOut = CLAMPF32(*minHeightOut, conn->minHeight, conn->maxHeight);
			*maxHeightOut = CLAMPF32(*maxHeightOut, conn->minHeight, conn->maxHeight);
		}
		else
		{
			*minHeightOut = data->minHeight+data->maxDistanceAnchorPos[1]-pos[1];
			*maxHeightOut = data->maxHeight+data->maxDistanceAnchorPos[1]-pos[1];
			*minHeightOut = CLAMPF32(*minHeightOut, conn->minHeight, conn->maxHeight);
			*maxHeightOut = CLAMPF32(*maxHeightOut, conn->minHeight, conn->maxHeight);
			MIN1(*minHeightOut, *maxHeightOut);
			MAX1(*maxHeightOut, *minHeightOut);
		}
	}

	if(conn)
	{
		Vec3 vBeaconConnectDir;
		subVec3(conn->destBeacon->pos, src->pos, vBeaconConnectDir);
		return dotVec3(vBeaconConnectDir, data->facingDirection) > 0 ? rand()%20 : rand()%10;
	}

	return rand()%20;
}

// -------------------------------------------------------------------------------------------------------
// Sets the distance within which this entity wanders for the current state
AUTO_EXPR_FUNC(ai) ACMD_NAME(WanderSetDist);
void exprFuncWanderSetDist(ExprContext* context, F32 wanderDist)
{
	FSMLDWander* mydata = getMyData(context, parse_FSMLDWander, 0);

	mydata->wanderDistSQR = SQR(wanderDist);
}

// -------------------------------------------------------------------------------------------------------
static int wanderCounter = 0;

static int wanderDistPrint = false;
AUTO_CMD_INT(wanderDistPrint, wanderDistPrint);

static int disableWander = false;
AUTO_CMD_INT(disableWander, disableWander);

static int wanderLimit = 100;
AUTO_CMD_INT(wanderLimit, wanderLimit);


AUTO_COMMAND_QUEUED();
void efWanderDecreaseWanderCount(ACMD_POINTER AIVarsBase* aib)
{
	if(aib->wandering)
	{
		aib->wandering = 0;
		--wanderCounter;
	}
	assert(wanderCounter >= 0);
}

#define AI_DEFAULT_WANDER_DIST 40


// ----------------------------------------------------------------------------------------------------------------
// returns true if any of the Entity's teammates are near the target position, 
//		or the entity's position if the given position is NULL
static int aiWander_IsTeammateAtLocation(Entity *e, SA_PARAM_OP_VALID const Vec3 vLoc)
{
	if (e->aibase->team)
	{
		Vec3 vEntPos;

		if (!vLoc)
		{	// get the entity's position instead
			entGetPos(e, vEntPos);
			vLoc = vEntPos;
		}

		// just checking ambient team for now
		// ignore teammates that are currently moving
		FOR_EACH_IN_EARRAY(e->aibase->team->members, AITeamMember, member)
		{
			if (member->memberBE != e)
			{	
				if (!member->memberBE->aibase->currentlyMoving)
				{
					Vec3 vMemberPos;
					entGetPos(member->memberBE, vMemberPos);
					if (distance3Squared(vMemberPos, vLoc) < SQR(3.25f))
						return true;
				}
			}
		}
		FOR_EACH_END
	}

	return false;
}


typedef void (*fpWanderPathCreate)(Entity *e, FSMLDWander *wanderData, NavPath* navPath, void *userData);



// -------------------------------------------------------------------------------------------------------
static int wanderInternal(Entity* e, ExprContext* context, Vec3 startPos, fpWanderPathCreate fpPathCreate, 
						  void *userData, char** errString)
{
	FSMLDWander* mydata = getMyData(context, parse_FSMLDWander, 0);
	AIVarsBase* aib = e->aibase;
	S32 iPartitionIdx = entGetPartitionIdx(e);

	if(!fpPathCreate || !aib->wandering && (disableWander || (wanderLimit && wanderCounter >= wanderLimit)))
		return 0;

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetCurStateTracker(context);
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers || !localData)
		{
			estrPrintf(errString, "Unable to call wander in this section - missing exit handlers");
			return -1;
		}

		if(!startPos)
			aiGetSpawnPos(e, aib, mydata->wanderHomePos);
		else
		{
			if(exprFuncAddConfigMod(e, context, "roamingLeash", "1", errString)!=ExprFuncReturnFinished)
				return ExprFuncReturnError;
			copyVec3(startPos, mydata->wanderHomePos);
		}

		++wanderCounter;

		if(!aib->onDeathCleanup)
			aib->onDeathCleanup = CommandQueue_Create(4 * sizeof(void*), false);

		aib->wandering = 1;
		QueuedCommand_efWanderDecreaseWanderCount(aib->onDeathCleanup, aib);

		QueuedCommand_efWanderDecreaseWanderCount(exitHandlers, aib);
		QueuedCommand_aiMovementResetPath(exitHandlers, e, aib);
		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDWander, localData, 0);
		mydata->addedExitHandlers = 1;
		
		// force a path on entry 
		mydata->lastPathFindTime = ABS_TIME_PARTITION(iPartitionIdx) - 1;
		mydata->wanderState = EWanderState_IDLE;
	}

	switch (mydata->wanderState)
	{
		xcase EWanderState_IDLE:
			if (mydata->lastPathFindTime <= ABS_TIME_PARTITION(iPartitionIdx) || aiWander_IsTeammateAtLocation(e, NULL) )
			{
				AIMovementFG* fg = aiMovementGetFG(e, aib);

				fpPathCreate(e, mydata, &fg->pathNormal, userData);

				FOR_EACH_IN_EARRAY(fg->pathNormal.waypoints, NavPathWaypoint, wp)
				{
					if(globalWPParanoia)
						assert(wp->dts.dts==DTS_NONE);
					dtsSetState(&wp->dts, DTS_INFG);
				}
				FOR_EACH_END

				if(!eaSize(&fg->pathNormal.waypoints))
				{
					PERFINFO_AUTO_START("Pathfind home", 1);
					aiMovementSetTargetPosition(e, aib, mydata->wanderHomePos, NULL, 0);
					PERFINFO_AUTO_STOP();
				}
				else
					aiMovementUpdateBGNavSearch(e, e->aibase);

				mydata->wanderState = EWanderState_WANDERING;
				mydata->lastPathFindTime = ABS_TIME_PARTITION(iPartitionIdx);
			}
		xcase EWanderState_WANDERING:
		{
			if (!aib->currentlyMoving)
			{
				F32 randIdleTime = (mydata->wanderIdleTimeAvg ? mydata->wanderIdleTimeAvg : 4.f);

				randIdleTime = aiAmbientRandomDuration(randIdleTime, 0.3f);

				mydata->wanderState = EWanderState_IDLE;

				mydata->lastPathFindTime = ABS_TIME_PARTITION(iPartitionIdx) + SEC_TO_ABS_TIME(randIdleTime);
			}
		}
	}
	

	if(wanderDistPrint)
	{
		Vec3 bePos;
		F32 dist;

		entGetPos(e, bePos);

		dist = distance3(aib->spawnPos, bePos);
		printf("%s (%d) is %f away from its spawn point\n", e->debugName, e->myRef, dist);
	}

	return 0;
}


// -------------------------------------------------------------------------------------------------------
void wanderGroundPathCreate(Entity *e, FSMLDWander *wanderData, NavPath* path, void *userData)
{
	BeaconScoreData scoredata = {0};
	Vec3 curPos;

	scoredata.e = e;

	copyVec3(wanderData->wanderHomePos, scoredata.maxDistanceAnchorPos);

	if(!wanderData->wanderDistSQR)
		scoredata.maxDistanceSQR = SQR(AI_DEFAULT_WANDER_DIST);
	else
		scoredata.maxDistanceSQR = wanderData->wanderDistSQR;

	entGetPos(e, curPos);

	{
		Vec2 pyFace; 
		entGetFacePY(e, pyFace);
		setVec3FromYaw(scoredata.facingDirection, pyFace[1]);
	}


	beaconSetPathFindEntity(entGetRef(e), 0, aiMovementGetPathEntHeight(e));
	beaconPathFindSimpleStart(entGetPartitionIdx(e), path, curPos, wanderData->wanderMaxPath, beaconScoreWander, &scoredata);
}


// -------------------------------------------------------------------------------------------------------
int wanderGroundInternal(Entity* e, ExprContext* context, Vec3 homePos, char** errString)
{
	return wanderInternal(e, context, homePos, wanderGroundPathCreate, NULL, errString);
}

// -------------------------------------------------------------------------------------------------------
// Makes this entity wander around close to its spawn. This consists of finding beacons within
// the specified area randomly and moving towards them, so it doesn't work if your map doesn't
// have beacons
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(Wander) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncWander(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_ERRSTRING errString)
{
	if(wanderGroundInternal(e, context, NULL, errString) < 0)
	{
		return ExprFuncReturnError;
	}
	else
	{
		return ExprFuncReturnFinished;
	}
}

// Makes this entity wander close to the given position
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(WanderAtPos) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncWanderAtPos(ACMD_EXPR_SELF Entity *e, ExprContext* context, ACMD_EXPR_LOC_MAT4_IN loc, ACMD_EXPR_ERRSTRING errString)
{
	if(wanderGroundInternal(e, context, loc[3], errString))
		return ExprFuncReturnError;
	else
		return ExprFuncReturnFinished;
}

typedef struct AirWanderParams
{
	F32 minHeight; 
	F32 maxHeight;
	int groundRelative;
} AirWanderParams;

// -------------------------------------------------------------------------------------------------------
void wanderAirPathCreate(Entity *e, FSMLDWander *wanderData, NavPath *path, AirWanderParams *airWanderParams)
{
	BeaconScoreData scoredata = {0};
	Vec3 curPos;

	scoredata.e = e;
	
	wanderData->airWander = 1;

	copyVec3(wanderData->wanderHomePos, scoredata.maxDistanceAnchorPos);

	if(!wanderData->wanderDistSQR)
		scoredata.maxDistanceSQR = SQR(AI_DEFAULT_WANDER_DIST);
	else
		scoredata.maxDistanceSQR = wanderData->wanderDistSQR;

	entGetPos(e, curPos);


	scoredata.groundRelativeHeights = !!airWanderParams->groundRelative;
	scoredata.minHeight = airWanderParams->minHeight;
	scoredata.maxHeight = airWanderParams->maxHeight;
	
	{
		Vec2 pyFace; 
		entGetFacePY(e, pyFace);
		setVec3FromYaw(scoredata.facingDirection, pyFace[1]);
	}

	beaconSetPathFindEntity(entGetRef(e), 0, aiMovementGetPathEntHeight(e));
	beaconSetPathFindEntityForceFly(true);
	curPos[1] += 0.5;
	beaconPathFindSimpleStart(entGetPartitionIdx(e), path, curPos, wanderData->wanderMaxPath, beaconScoreWanderAir, &scoredata);
}

// -------------------------------------------------------------------------------------------------------
int wanderAirInternal(Entity* e, ExprContext* context, Vec3 homePos, F32 minHeight, F32 maxHeight, int groundRelative, char **errString)
{
	AirWanderParams params;

	params.minHeight = minHeight;
	params.maxHeight = maxHeight;
	params.groundRelative = groundRelative;

	return wanderInternal(e, context, homePos, (fpWanderPathCreate) wanderAirPathCreate, &params, errString);
}

// -------------------------------------------------------------------------------------------------------
// Makes this entity wander around close to its spawn. This consists of finding air beacons within
// the specified area randomly and moving towards them, so it doesn't work if your map doesn't
// have beacons
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(WanderAir) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncWanderAir(ACMD_EXPR_SELF Entity* e, ExprContext* context, F32 minHeight, F32 maxHeight, int groundRelative, ACMD_EXPR_ERRSTRING errString)
{
	if(wanderAirInternal(e, context, NULL, minHeight, maxHeight, groundRelative, errString) < 0)
	{
		return ExprFuncReturnError;
	}
	else
	{
		return ExprFuncReturnFinished;
	}
}

// Makes this entity wander around close to the given position in the air
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(WanderAtPosAir) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncWanderAtPosAir(ACMD_EXPR_SELF Entity* e, ExprContext* context, ACMD_EXPR_LOC_MAT4_IN loc, F32 minHeight, F32 maxHeight, int groundRelative, ACMD_EXPR_ERRSTRING errString)
{
	if(wanderAirInternal(e, context, loc[3], minHeight, maxHeight, groundRelative, errString) < 0)
	{
		return ExprFuncReturnError;
	}
	else
	{
		return ExprFuncReturnFinished;
	}
}

//--------------------------------------------------------------------------------------------------------
// Disoriented
//--------------------------------------------------------------------------------------------------------

static int disorientedScoreWander(Beacon* src, BeaconConnection* conn, BeaconScoreData *data, F32 *unused1, F32 *unused2){
	float* pos;
	Vec3 vCurPos;
	F32 fDistSq = 0.0f;
	F32 distanceToAnchorSQR = 0.0f;
	int iResult;

	if(conn){
		pos = conn->destBeacon->pos;
	}else{
		pos = src->pos;
	}

	if (rand()%2 != 0)
	{
		if(aiShouldAvoidBeacon(entGetRef(data->e), src, data->minHeight))
			return 0;
		if(conn)
		{
			if(aiShouldAvoidBeacon(entGetRef(data->e), conn->destBeacon, data->minHeight))
				return 0;
			if(aiShouldAvoidLine(entGetRef(data->e), src, NULL, conn->destBeacon, data->minHeight, NULL))
				return 0;
		}
	}
	
	distanceToAnchorSQR = distance3Squared(data->maxDistanceAnchorPos, pos);

	if(data->maxDistanceSQR)
	{
		if(distanceToAnchorSQR > data->maxDistanceSQR)
			return 0;
	}

/*	if(conn)
	{
		Vec3 vBeaconConnectDir;
		subVec3(conn->destBeacon->pos, src->pos, vBeaconConnectDir);
		return dotVec3(vBeaconConnectDir, data->facingDirection) > 0 ? rand()%20 : rand()%10;
	}*/

	entGetPos(data->e, vCurPos);
	fDistSq = distance3SquaredXZ(vCurPos,pos);
	
	iResult = 100+MAX(0,100.0f-distanceToAnchorSQR)+rand()%100;
	if (fDistSq < 4.0f)
	{
		iResult /= 2;
	}
	else
	{
		Vec3 vBeaconDir;
		subVec3(pos, vCurPos, vBeaconDir);
		vBeaconDir[1] = 0.0f;
		normalVec3XZ(vBeaconDir);

		iResult += dotVec3(vBeaconDir, data->facingDirection) * 50.0f;
	}
	
	return iResult;
}

AUTO_COMMAND_QUEUED();
void efDisorientedEnd(ACMD_POINTER AIVarsBase* aib)
{

}

// -------------------------------------------------------------------------------------------------------
void disorientedPathCreate(Entity *e, FSMLDDisoriented *pData, NavPath* pPath, void *userData)
{
	BeaconScoreData scoredata = {0};
	Vec3 curPos;

	scoredata.e = e;

	copyVec3(pData->vDestinationPos, scoredata.maxDistanceAnchorPos);

	// we're not going to actually go all the way to this beacon, so, don't worry about it
	scoredata.maxDistanceSQR = SQR(50.0f);

	entGetPos(e, curPos);

	{
		Vec2 pyFace; 
		entGetFacePY(e, pyFace);
		setVec3FromYaw(scoredata.facingDirection, pyFace[1]);
	}

	beaconSetPathFindEntity(entGetRef(e), 0, aiMovementGetPathEntHeight(e));
	beaconPathFindSimpleStart(entGetPartitionIdx(e), pPath, curPos, 1, disorientedScoreWander, &scoredata);
}

static int disorientedInternal(Entity* e, ExprContext* context, void *userData, char** errString)
{
	FSMLDDisoriented* mydata = getMyData(context, parse_FSMLDDisoriented, 0);
	AIVarsBase* aib = e->aibase;
	S32 iPartitionIdx = entGetPartitionIdx(e);
	F32 fTraveledDistSq = 0.0f;
	Vec3 vCurPos;
	AIMovementFG* fg = aiMovementGetFG(e, aib);

	entGetPos(e, vCurPos);

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetCurStateTracker(context);
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers || !localData)
		{
			estrPrintf(errString, "Unable to call disorient in this section - missing exit handlers");
			return -1;
		}

		// TODO - not right
		copyVec3(vCurPos, mydata->vDestinationPos);

		if(!aib->onDeathCleanup)
			aib->onDeathCleanup = CommandQueue_Create(4 * sizeof(void*), false);

		//QueuedCommand_efDisorientedEnd(aib->onDeathCleanup, aib);

		//QueuedCommand_efDisorientedEnd(exitHandlers, aib);
		QueuedCommand_aiMovementResetPath(exitHandlers, e, aib);
		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDDisoriented, localData, 0);
		mydata->addedExitHandlers = 1;
	}
	else
	{
		// Check how far I've stumbled
		fTraveledDistSq = distance3SquaredXZ(mydata->vJagStartPos,vCurPos);
	}

	if ((!eaSize(&fg->pathNormal.waypoints) && !aib->currentlyMoving)|| fTraveledDistSq > SQR(5.0f))
	{
		disorientedPathCreate(e, mydata, &fg->pathNormal, userData);

		FOR_EACH_IN_EARRAY(fg->pathNormal.waypoints, NavPathWaypoint, wp)
		{
			if(globalWPParanoia)
				assert(wp->dts.dts==DTS_NONE);
			dtsSetState(&wp->dts, DTS_INFG);
		}
		FOR_EACH_END

		if(!eaSize(&fg->pathNormal.waypoints))
		{
			PERFINFO_AUTO_START("Pathfind home", 1);
			aiMovementSetTargetPosition(e, aib, mydata->vDestinationPos, NULL, 0);
			PERFINFO_AUTO_STOP();
		}
		else
			aiMovementUpdateBGNavSearch(e, e->aibase);

		copyVec3(vCurPos,mydata->vJagStartPos);
	}

	return 0;
}

// -------------------------------------------------------------------------------------------------------
// Makes this entity wander around close to its spawn. This consists of finding beacons within
// the specified area randomly and moving towards them, so it doesn't work if your map doesn't
// have beacons
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(DoDisoriented) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncDoDisoriented(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ERRSTRING errString)
{
	if(disorientedInternal(be, context, NULL, errString) < 0)
	{
		return ExprFuncReturnError;
	}
	else
	{
		return ExprFuncReturnFinished;
	}
}

// -------------------------------------------------------------------------------------------------------
// Patrolling
// -------------------------------------------------------------------------------------------------------


ExprFuncReturnVal aiMovementDoEncounterPatrol(ACMD_EXPR_SELF Entity* be, ExprContext* context, int useOffset, int useNearest, ACMD_EXPR_ERRSTRING errString)
{
	GameEncounter *pEncounter;
	const char *pcPatrolRoute;
	AIVarsBase *aib = be->aibase;

	if(!be->pCritter )
	{
		Vec3 bePos;
		entGetPos(be, bePos);
		estrPrintf(errString, "Critter %s at %.2f, %.2f, %.2f does not have a critter, so cannot do Patrol(), please specify a route with PatrolNamed()",
			be->debugName, vecParamsXYZ(bePos));
		return ExprFuncReturnError;
	}

	pEncounter = be->pCritter->encounterData.pGameEncounter;
	if (pEncounter)
	{
		pcPatrolRoute = encounter_GetPatrolRoute(pEncounter);
	}
	else if (gConf.bAllowOldEncounterData)
	{
		OldStaticEncounter* staticEnc;

		if(!be->pCritter->encounterData.parentEncounter)
		{
			static int errors = 0;
			if(++errors < 5)
				estrPrintf(errString, "Critter %s has an FSM action Patrol() in it without an encounter", be->debugName);

			return ExprFuncReturnError;
		}

		staticEnc = GET_REF(be->pCritter->encounterData.parentEncounter->staticEnc);

		if(!staticEnc)
		{
			devassertmsg(0, "It's a code bug if there's no static encounter here");
			return ExprFuncReturnFinished;
		}
		pcPatrolRoute = staticEnc->patrolRouteName;
		if(!pcPatrolRoute || !pcPatrolRoute[0])
		{
			static int errors = 0;

			if(++errors < 5)
				ErrorFilenamef(staticEnc->pchFilename, "Encounter %s (%s) has an FSM action Patrol() in it, but no patrol is specified", staticEnc->name, staticEnc->pchFilename);

			// this is returnfinished because it wants to blame the encounter instead of the fsm
			return ExprFuncReturnFinished;
		}
		return aiMovementDoPatrol(be, context, pcPatrolRoute, useOffset, useNearest, errString);
	}
	else
	{
		devassertmsg(0, "It's a code bug if there's no encounter here");
		return ExprFuncReturnFinished;
	}

	if(!pcPatrolRoute || !pcPatrolRoute[0])
	{
		static int errors = 0;

		if(++errors < 5)
			ErrorFilenamef(encounter_GetFilename(pEncounter), "Encounter %s has an FSM action Patrol() in it, but no patrol is specified", pEncounter->pcName);

		// this is returnfinished because it wants to blame the encounter instead of the fsm
		return ExprFuncReturnFinished;
	}

	return aiMovementDoPatrol(be, context, pcPatrolRoute, useOffset, useNearest, errString);
}

// Runs the patrol specified in the entity's encounter, resumes at nearest waypoint
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(PatrolResumeNearest) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncPatrolResumeNearest(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ERRSTRING errString)
{
	return aiMovementDoEncounterPatrol(be, context, false, true, errString);
}

// Runs the patrol specified by the entity's encounter offset by the actor's relative position, resumes at nearest waypoint
// in the encounter
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(PatrolResumeNearestOffset);
ExprFuncReturnVal exprFuncPatrolResumeNearestOffset(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ERRSTRING errString)
{
	return aiMovementDoEncounterPatrol(be, context, true, true, errString);
}

// Runs the patrol specified in the entity's encounter
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(Patrol) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncPatrol(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ERRSTRING errString)
{
	return aiMovementDoEncounterPatrol(be, context, false, false, errString);
}

// Runs the patrol specified by the entity's encounter offset by the actor's relative position
// in the encounter
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(PatrolOffset);
ExprFuncReturnVal exprFuncPatrolOffset(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ERRSTRING errString)
{
	return aiMovementDoEncounterPatrol(be, context, true, false, errString);
}

// Returns whether the current entity has finished running into its door (i.e. is done with
// its animation)
// Currently these animations are not implemented so this will return true as soon as RunIntoDoor
// is called, but you have to wait to transition states until this is true
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(RunIntoDoorFinished);
int exprFuncRunIntoDoorFinished(ExprContext* context)
{
	FSMLDRunIntoDoor* mydata = getMyData(context, parse_FSMLDRunIntoDoor, 0);
	return mydata->finishedRunningIntoDoor;
}

// WARNING: Finds the nearest door and runs into it, this does not kill the entity. Make 
// sure to call RunIntoDoorFinished() before continuing with your FSM behavior
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(RunIntoDoor) ACMD_EXPR_FUNC_COST_MOVEMENT;
ExprFuncReturnVal exprFuncRunIntoDoor(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	FSMLDRunIntoDoor* mydata = getMyData(context, parse_FSMLDRunIntoDoor, 0);
	Vec3 myPos;

	entGetPos(be, myPos);

	if(!mydata->hasDoor)
		mydata->hasDoor = interactable_FindClosestDoor(myPos, mydata->myDoorPos);

	if(!mydata->hasDoor)
	{
		*errString = "Using RunIntoDoor on a map without a door";
		return ExprFuncReturnError;
	}

	if(!mydata->addedExitHandlers)
	{
		FSMStateTrackerEntry* tracker = exprContextGetCurStateTracker(context);
		CommandQueue* exitHandlers = NULL;
		ExprLocalData*** localData = NULL;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localData);

		if(!exitHandlers)
		{
			*errString = "Unable to call RunIntoDoor in this section - missing exit handlers";
			return ExprFuncReturnError;
		}

		QueuedCommand_aiMovementResetPath(exitHandlers, be, be->aibase);
		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDRunIntoDoor, localData, 0);

		aiMovementSetTargetPosition(be, be->aibase, mydata->myDoorPos, NULL,
			AI_MOVEMENT_TARGET_CRITICAL);
		mydata->addedExitHandlers = 1;
	}

	if(distance3SquaredXZ(myPos, mydata->myDoorPos) < SQR(10))
		mydata->finishedRunningIntoDoor = 1;

	return ExprFuncReturnFinished;
}

AUTO_COMMAND_QUEUED();
void exprCleanUpExternVarOverride(ACMD_POINTER ExprContext *context, const char* category, const char* name, ACMD_POINTER MultiVal *val)
{
	MultiValDestroy(val);

	exprContextClearOverrideExternVar(context, category, name, NULL);	
}

ExprFuncReturnVal exprFuncOverrideExternVarCurState(ExprContext* context, const char* category, const char* name, MultiVal *val, ACMD_EXPR_ERRSTRING errString)
{
	U64 key = (U64)name;
	FSMLDGenericSetData *data = getMyData(context, parse_FSMLDGenericSetData, key);

	if(!data->setData)
	{
		CommandQueue *exitHandlers = NULL;
		ExprLocalData ***localdata = NULL;

		data->setData = true;

		exprContextGetCleanupCommandQueue(context, &exitHandlers, &localdata);

		if(!exitHandlers)
		{
			*errString = "Unable to call override extern var in this section - missing exit handlers";
			return ExprFuncReturnError;
		}

		QueuedCommand_deleteMyData(exitHandlers, context, parse_FSMLDGenericSetData, localdata, key);
		QueuedCommand_exprCleanUpExternVarOverride(exitHandlers, context, category, name, val);

		return exprContextOverrideExternVar(context, category, name, val);
	}

	return ExprFuncReturnFinished;
}

// Overrides the variable to the given value for the current state only
AUTO_EXPR_FUNC(util) ACMD_NAME(OverrideExternStringVarCurState);
ExprFuncReturnVal exprFuncOverrideExternStringVarCurState(ExprContext* context, const char* category, const char* name, char *value, ACMD_EXPR_ERRSTRING errString)
{
	MultiVal *val = MultiValCreate();

	val->type = MULTI_STRING;
	val->str = strdup(value);

	return exprFuncOverrideExternVarCurState(context, category, name, val, errString);
}

// Overrides the variable to the given value for the current state only
AUTO_EXPR_FUNC(util) ACMD_NAME(OverrideExternIntVarCurState);
ExprFuncReturnVal exprFuncOverrideExternIntVarCurState(ExprContext* context, const char* category, const char* name, S32 value, ACMD_EXPR_ERRSTRING errString)
{
	MultiVal *val = MultiValCreate();

	val->type = MULTI_INT;
	val->intval = value;

	return exprFuncOverrideExternVarCurState(context, category, name, val, errString);
}

// Overrides the variable to the given value for the current state only
AUTO_EXPR_FUNC(util) ACMD_NAME(OverrideExternFloatVarCurState);
ExprFuncReturnVal exprFuncOverrideExternFloatVarCurState(ExprContext* context, const char* category, const char* name, F32 value, ACMD_EXPR_ERRSTRING errString)
{
	MultiVal *val = MultiValCreate();

	val->type = MULTI_FLOAT;
	val->floatval = value;

	return exprFuncOverrideExternVarCurState(context, category, name, val, errString);
}

// Returns whether the current entity has finished running out of its door. Currently this returns
// true immediately as the animations are not yet implemented
AUTO_EXPR_FUNC(ai) ACMD_NAME(RunOutOfDoorFinished);
int exprFuncRunOutOfDoorFinished(ExprContext* context)
{
	FSMLDRunOutOfDoor* mydata = getMyData(context, parse_FSMLDRunOutOfDoor, 0);
	return mydata->finishedRunningOutOfDoor;
}

// WARNING: Finds the closest door and runs out of it. See note at RunIntoDoor on proper behavior
// continuation after calling this
AUTO_EXPR_FUNC(ai_movement) ACMD_NAME(RunOutOfDoor);
ExprFuncReturnVal exprFuncRunOutOfDoor(ACMD_EXPR_SELF Entity* be, ExprContext* context, ACMD_EXPR_ERRSTRING_STATIC errString)
{
	FSMLDRunOutOfDoor* mydata = getMyData(context, parse_FSMLDRunOutOfDoor, 0);
	Vec3 myPos;

	entGetPos(be, myPos);

	if(!mydata->hasDoor)
		mydata->hasDoor = interactable_FindClosestDoor(myPos, mydata->myDoorPos);

	if(!mydata->hasDoor)
	{
		*errString = "Using RunIntoDoor on a map without a door";
		return ExprFuncReturnError;
	}

	mydata->finishedRunningOutOfDoor = 1;

	return ExprFuncReturnFinished;
}

AUTO_COMMAND ACMD_NAME(FindCombatBeacon) ACMD_LIST(gEntConCmdList);
void FindCombatBeacon(Entity* e)
{
	Vec3 sourcePos;
	NavPath path = {0};
	entGetPos(e, sourcePos);
	beaconSetPathFindEntity(entGetRef(e), 0, aiMovementGetPathEntHeight(e));
	beaconPathFind(entGetPartitionIdx(e), &path, sourcePos, zerovec3, ENTDEBUGNAME(e));

	navPathClear(&path);
}

typedef struct AICloseRangeMovementExpr
{
	Expression* beaconRequires;
	Expression* beaconRating;
}AICloseRangeMovementExpr;

void aiCloseRangeMovement(Entity* e, AIVarsBase* aib, Vec3 targetPos, F32 radius)
{
	Array* beaconArray;
	int size;
	int i;

	beaconArray = beaconMakeSortedNearbyBeaconArray(entGetPartitionIdx(e), targetPos, radius);

	size = beaconArray->size;

	for(i = 0; i < size; i++)
	{
		Beacon* b = beaconArray->storage[i];
		if(b->userFloat > radius)
			break;


	}
}

// returns whether a position was found, not whether the rays collided
int aiFindRunToPos(Entity* be, AIVarsBase* aib, const F32* myPos, const F32* targetPos,
				   const F32* idealVec, const F32* defaultPos, F32* outPos, F32 maxAngle)
{
	int collided;
	int bestNoLOSFromMePosFound = false;
	Vec3 bestNoLOSFromMePos;
	F32 angle;

	copyVec3(defaultPos, outPos);

	collided = checkWorldCollideFromAngleAndMe(be, aib, targetPos, targetPos, idealVec, 0,
		outPos, bestNoLOSFromMePos, bestNoLOSFromMePosFound, &bestNoLOSFromMePosFound);

	for(angle = PI/4; collided && (angle < maxAngle || nearf(angle, maxAngle)); angle += PI/4)
	{
		collided = checkWorldCollideFromAngleAndMe(be, aib, targetPos, targetPos, idealVec, angle,
			outPos, bestNoLOSFromMePos, bestNoLOSFromMePosFound, &bestNoLOSFromMePosFound);

		if(collided)
		{
			collided = checkWorldCollideFromAngleAndMe(be, aib, targetPos, targetPos, idealVec,
				-1 * angle, outPos, bestNoLOSFromMePos, bestNoLOSFromMePosFound,
				&bestNoLOSFromMePosFound);
		}
	}

	// at this point we might as well use a place that's harder to get to but ok
	if(collided && bestNoLOSFromMePosFound)
	{
		copyVec3(bestNoLOSFromMePos, outPos);
		return true;
	}

	AI_DEBUG_PRINT(be, AI_LOG_MOVEMENT, 7,
		"aiFindRunToPos: Chose " LOC_PRINTF_STR ", default was " LOC_PRINTF_STR,
				vecParamsXYZ(outPos),
				vecParamsXYZ(defaultPos));

	return !collided;
}

void aiDestroyLocalData(ExprLocalData *data)
{
	static DestroyMyLocalData localDataDestroyer;

	localDataDestroyer.localdata = data;
	StructDeInit(parse_DestroyMyLocalData, &localDataDestroyer);

	// I wish this would just work, but Alex told me to do the DeInit hack instead
	//StructDestroy(parse_FSMLocalData, data);
}

AUTO_RUN;
void aiRegisterLocalDataFree(void)
{
	fsmSetLocalDataDestroyFunc(aiDestroyLocalData);
	fsmSetPartitionTimeCallback(mapState_GetTime);
}

AUTO_FIXUPFUNC;
TextParserResult fixupFSMLDGenericEArray(FSMLDGenericEArray *data, enumTextParserFixupType eType, void *pExtraData)
{
	switch(eType)
	{
		xcase FIXUPTYPE_DESTRUCTOR: {
			eaDestroy(&data->myEArray);
		}
	}
	return PARSERESULT_SUCCESS;
}

AUTO_FIXUPFUNC;
TextParserResult fixupFSMLDAnimList(FSMLDAnimList *data, enumTextParserFixupType type, void *extradata)
{
	switch(type)
	{
		xcase FIXUPTYPE_DESTRUCTOR: {
			if(data->animListCommandQueue)
			{
				CommandQueue_Destroy(data->animListCommandQueue);
				data->animListCommandQueue = NULL;
			}
		}
	}

	return PARSERESULT_SUCCESS;
}

#include "aiMovement_h_ast.c"
#include "aiMovement_c_ast.c"
