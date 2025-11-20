#include "beaconPath.h"

#include "eset.h"
#include "MemTrack.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

int DEFAULT_LATELINK_aiShouldAvoidBeacon(U32 entref, Beacon* beacon, F32 height)
{
	return 0;
}

int DEFAULT_LATELINK_aiShouldAvoidLine(U32 entref, Beacon *bcnSrc, const Vec3 pos, Beacon *bcnDst, F32 height, BeaconPartitionData * pStartBeaconPartitionData)
{
	return 0;
}

#if !PLATFORM_CONSOLE

#include <limits.h>
#include <stddef.h>
#include "beaconPrivate.h"
#include "beaconConnection.h"
#include "beaconAStar.h"
#include "rand.h"
#include "MemoryPool.h"
#include "net/net.h"
#include "WorldGrid.h"
#include "CommandQueue.h"
#include "logging.h"

#define MIL (1000000)
#define BEACON_LOG_PATH_THRESHOLD 80*MIL
S64 bcnPathLogCycleRanges[] = { 1 * MIL, 5 * MIL, 10 * MIL, 50 * MIL, 100 * MIL, 1000 * MIL };
BeaconPathLogEntrySet* bcnPathLogSets[ARRAY_SIZE(bcnPathLogCycleRanges) + 1];

#define DIST_TO_COST_SCALE (128)

#define BEACON_PATHFIND_CPU_TICK_LIMIT 400000000

static Array				staticBeaconSortArray;
static int					staticBeaconSortMinY;
static int					staticBeaconSortMaxY;
static U32					staticDebugEntity;
static Packet*				staticDebugPacket = NULL;
static int					staticAvailableBeaconCount = 0;
static PathFindEntityData	pathFindEntity;

int g_BcnExpensivePathsFail = true;
AUTO_CMD_INT(g_BcnExpensivePathsFail, BcnExpensivePathsFail);

int g_BcnForcePathfindTimeout = false;
AUTO_CMD_INT(g_BcnForcePathfindTimeout, BcnForcePathfindTimeout);

typedef int (*BeaconConnectionFilter)(Beacon* b, BeaconConnection *conn, int ground);
typedef int (*BlockConnectionFilter)(BeaconBlock* block, BeaconBlockConnection *blockConn, int ground);

static struct {
	IntEntRefCallback partitioncb;
	BeaconPathDoorCallback doorcb;
	BeaconEntRefCanFlyCallback flycb;
	BeaconEntRefIsFlyingCallback isflyingcb;
	BeaconEntRefAlwaysFlyingCallback alwaysflyingcb;
	BeaconEntRefNeverFlyingCallback neverflyingcb;
	BeaconEntRefGetTurnRateCallback turnratecb;
	BeaconEntRefGetJumpHeightCallback jumpheightcb;
	BeaconEntRefGetJumpCostsCallback jumpvertcb;
	BeaconEntRefGetJumpCostsCallback jumpdistcb;
	BeaconEntRefGetJumpCostsCallback jumpcostcb;
} pathFindCallbacks;

void beaconSetPathFindPartitionCallback(IntEntRefCallback cb)
{
	pathFindCallbacks.partitioncb = cb;
}

void beaconSetPathFindCanFlyCallback(BeaconEntRefCanFlyCallback cb)
{
	pathFindCallbacks.flycb = cb;
}

void beaconSetPathFindIsFlyingCallback(BeaconEntRefCanFlyCallback cb)
{
	pathFindCallbacks.isflyingcb = cb;
}

void beaconSetPathFindAlwaysFlyingCallback(BeaconEntRefAlwaysFlyingCallback cb)
{
	pathFindCallbacks.alwaysflyingcb = cb;
}

void beaconSetPathFindNeverFlyingCallback(BeaconEntRefNeverFlyingCallback cb)
{
	pathFindCallbacks.neverflyingcb = cb;
}

void beaconSetPathFindTurnRateCallback(BeaconEntRefGetTurnRateCallback cb)
{
	pathFindCallbacks.turnratecb = cb;
}

void beaconSetPathFindJumpHeightCallback(BeaconEntRefGetJumpHeightCallback cb)
{
	pathFindCallbacks.jumpheightcb = cb;
}

void beaconSetPathFindJumpHeightMultCallback(BeaconEntRefGetJumpCostsCallback cb)
{
	pathFindCallbacks.jumpvertcb = cb;
}

void beaconSetPathFindJumpDistMultCallback(BeaconEntRefGetJumpCostsCallback cb)
{
	pathFindCallbacks.jumpdistcb = cb;
}

void beaconSetPathFindJumpCostCallback(BeaconEntRefGetJumpCostsCallback cb)
{
	pathFindCallbacks.jumpcostcb = cb;
}

S32 beaconPathLogSetsGetSize()
{
	return ARRAY_SIZE(bcnPathLogSets);
}

S64 beaconPathLogGetSetLimitMin(S32 set)
{
	if(set>=1 && set<ARRAY_SIZE(bcnPathLogCycleRanges)+1)
		return bcnPathLogCycleRanges[set-1];
	return 0;
}

S64 beaconPathLogGetSetLimitMax(S32 set)
{
	if(set>=0 && set<ARRAY_SIZE(bcnPathLogCycleRanges))
		return bcnPathLogCycleRanges[set];
	return INT_MAX;
}

BeaconPathLogEntry* bcnPathLogAddEntry(S64 source, S64 target, S64 path){
	BeaconPathLogEntry* entry;
	int i;
	S64 cycles = source+target+path;

	for(i = 0; i < ARRAY_SIZE(bcnPathLogCycleRanges); i++){
		if(cycles <= bcnPathLogCycleRanges[i]){
			break;
		}
	}

	if(!bcnPathLogSets[i]){
		bcnPathLogSets[i] = calloc(sizeof(bcnPathLogSets[i][0]), 1);
		bcnPathLogSets[i]->max_size = 100;
		assert(bcnPathLogSets[i]);
	}

	if(++bcnPathLogSets[i]->cur >= bcnPathLogSets[i]->max_size){
		bcnPathLogSets[i]->cur = 0;
	}

	if(bcnPathLogSets[i]->cur >= eaSize(&bcnPathLogSets[i]->entries))
	{
		entry = calloc(1, sizeof(BeaconPathLogEntry));

		eaPush(&bcnPathLogSets[i]->entries, entry);
	}
	else
		entry = bcnPathLogSets[i]->entries[bcnPathLogSets[i]->cur];

	if(entry->triviaStr)
		StructFreeStringSafe(&entry->triviaStr);

	ZeroStruct(entry);
	
	entry->totalCycles = cycles;
	entry->findSource = source;
	entry->findTarget = target;
	entry->findPath = path;

	return entry;
}

void beaconSetPathFindEntity(U32 entref, F32 maxJumpHeight, F32 height)
{
	pathFindEntity.entref			= entref;
	pathFindEntity.canFly			= 0;
	pathFindEntity.alwaysFly		= 0;
	pathFindEntity.partition		= 0;
	pathFindEntity.noRaised			= 0;

	if(pathFindCallbacks.partitioncb && combatBeaconArray.size < BEACON_PARTITION_LIMIT)
		pathFindEntity.partition = pathFindCallbacks.partitioncb(entref);
	if(pathFindCallbacks.flycb)
	{
		pathFindEntity.canFly = pathFindCallbacks.flycb(entref);
	}
	if(pathFindCallbacks.alwaysflyingcb)
	{
		pathFindEntity.alwaysFly = pathFindCallbacks.alwaysflyingcb(entref);
		pathFindEntity.canFly |= pathFindEntity.alwaysFly;
	}
	if(pathFindCallbacks.neverflyingcb)
	{
		if(pathFindCallbacks.neverflyingcb(entref))
		{
			pathFindEntity.alwaysFly = 0;
			pathFindEntity.canFly = 0;
		}
	}
	if(pathFindCallbacks.turnratecb)
	{
		pathFindEntity.turnRate = pathFindCallbacks.turnratecb(entref);
		if(pathFindEntity.turnRate>360)
			pathFindEntity.turnRate = 0;
	}
	if(!maxJumpHeight && pathFindCallbacks.jumpheightcb)
	{
		pathFindEntity.maxJumpHeight = pathFindCallbacks.jumpheightcb(entref);
	}
	else
		pathFindEntity.maxJumpHeight	= maxJumpHeight;
	if(pathFindCallbacks.jumpcostcb)
	{
		pathFindEntity.jumpCostInFeet = pathFindCallbacks.jumpcostcb(entref);
	}
	else
		pathFindEntity.jumpCostInFeet = 10;
	if(pathFindCallbacks.jumpvertcb)
	{
		pathFindEntity.jumpHeightCostMult = pathFindCallbacks.jumpvertcb(entref);
	}
	else
		pathFindEntity.jumpHeightCostMult = 2;
	if(pathFindCallbacks.jumpdistcb)
	{
		pathFindEntity.jumpDistCostMult = pathFindCallbacks.jumpdistcb(entref);
	}
	else
		pathFindEntity.jumpDistCostMult = 2;

	pathFindEntity.height			= height;
	pathFindEntity.maxJumpXZDiffSQR	= SQR(pathFindEntity.maxJumpHeight * 2);
	pathFindEntity.useEnterable		= 1;
	pathFindEntity.useAvoid			= 0;
	pathFindEntity.noFlyInitialYCostMult = 3;

	beaconCheckBlocksNeedRebuild(pathFindEntity.partition);
}

void beaconSetPathFindEntityUseAvoid(U32 allow)
{
	pathFindEntity.useAvoid = !!allow;
}

void beaconSetPathFindEntityForceFly(U32 forceFly)
{
	pathFindEntity.alwaysFly = !!forceFly;
}

void beaconSetPathFindEntityNoRaised(U32 noRaised)
{
	pathFindEntity.noRaised = !!noRaised;
}

void beaconSetPathFindEntityAsParameters(F32 maxJumpHeight, F32 height, S32 canFly, U32 useEnterable, F32 turnRate, int partition){
	pathFindEntity.entref				= 0;
	pathFindEntity.partition			= partition;
	pathFindEntity.canFly				= canFly;
	pathFindEntity.alwaysFly			= canFly;
	pathFindEntity.height				= height;
	pathFindEntity.turnRate				= turnRate;
	pathFindEntity.jumpDistCostMult		= 2;
	pathFindEntity.jumpHeightCostMult	= 2;
	pathFindEntity.jumpCostInFeet		= 10;
	pathFindEntity.maxJumpHeight		= maxJumpHeight;
	pathFindEntity.maxJumpXZDiffSQR		= SQR(maxJumpHeight * 2);
	pathFindEntity.useEnterable			= useEnterable;
	pathFindEntity.pathLimited			= 0;
	pathFindEntity.noRaised				= 0;

	if(combatBeaconArray.size > BEACON_PARTITION_LIMIT)
		pathFindEntity.partition = 0;

	beaconCheckBlocksNeedRebuild(pathFindEntity.partition);
}

void beaconSetDebugClient(U32 entityRef){
	staticDebugEntity = entityRef;
}

void dtsSetState_dbg(DTS_Struct *st, DataThreadState state, const char* file, int line)
{
	st->dts = state;
	st->dts_file[state] = file;
	st->dts_line[state] = line;
}

//****************************************************
// NavPathWaypoint
//****************************************************

//static ThreadSafeMemoryPool NavPathWayPointPool;
//MP_DEFINE(NavPathWaypoint);

static ESet s_wpTrackerSet = NULL;
CRITICAL_SECTION s_wpStashCS;
int disableWpTracking = 0;
AUTO_CMD_INT(disableWpTracking, disableWpTracking);

void beaconPathCheckWaypointIntegrity(const NavPathWaypoint *wp)
{
	EnterCriticalSection(&s_wpStashCS);
	assert(!eSetFind(&s_wpTrackerSet, wp));
	LeaveCriticalSection(&s_wpStashCS);
}

NavPathWaypoint* createNavPathWaypointEx(const char* file, int line)
{
	NavPathWaypoint* wp;

	wp = calloc(sizeof(NavPathWaypoint), 1);

	if(!disableWpTracking && s_wpTrackerSet)
	{
		EnterCriticalSection(&s_wpStashCS);
		assert(eSetAdd(&s_wpTrackerSet, wp));
		LeaveCriticalSection(&s_wpStashCS);
	}

	dtsSetState_dbg(&wp->dts, DTS_NONE, file, line);

	return wp;
}

void destroyNavPathWaypointEx(NavPathWaypoint* wp, const char* file, int line)
{
	if(!wp)
		return;

	if(wp->commandsWhenPassed)
	{
		CommandQueue_Destroy(wp->commandsWhenPassed);
		wp->commandsWhenPassed = NULL;
	}

	if(!disableWpTracking && s_wpTrackerSet)
	{
		EnterCriticalSection(&s_wpStashCS);
		assert(eSetRemove(&s_wpTrackerSet, wp));
		LeaveCriticalSection(&s_wpStashCS);
	}

	free(wp);
}

void navPathAddTail(NavPath* path, NavPathWaypoint* wp)
{
	eaPush(&path->waypoints, wp);
}

void navPathAddHead(NavPath* path, NavPathWaypoint* wp)
{
	eaInsert(&path->waypoints, wp, 0);
}

NavPathWaypoint* navPathGetTargetWaypoint(NavPath* path)
{
	if(path->curWaypoint < 0 || path->curWaypoint >= eaSize(&path->waypoints))
		return NULL;

	return path->waypoints[eaSize(&path->waypoints)-1];
}

int navPathGetNextWaypoint(NavPath* path, int* curWaypoint, int* pingpongRev)
{
	int endOfPath = false;
	int num = eaSize(&path->waypoints);
	int nextWaypoint;
	
	if(*pingpongRev)
		nextWaypoint = *curWaypoint - 1;
	else
		nextWaypoint = *curWaypoint + 1;

	if(path->pingpong)
	{
		if(nextWaypoint == -1)
		{
			devassert(*pingpongRev);
			*pingpongRev = 0;
			nextWaypoint = 1;
			endOfPath = true;
		}
		else if(nextWaypoint == num)
		{
			devassert(!*pingpongRev);
			*pingpongRev = 1;
			nextWaypoint = num - 2;
			endOfPath = true;
		}
	}
	else
	{
		if(nextWaypoint == num)
		{
			endOfPath = true;

			if(path->circular)
				nextWaypoint = 0;
			else
				nextWaypoint = -1;
		}
	}

	if(*curWaypoint == nextWaypoint)
		*curWaypoint = -1;
	else
		*curWaypoint = nextWaypoint;

	return endOfPath;
}

int navPathUpdateNextWaypoint(NavPath* path)
{
	int pingpongRev = path->pingpongRev;
	int endOfPath = navPathGetNextWaypoint(path, &path->curWaypoint, &pingpongRev);
	path->pingpongRev = pingpongRev;
	return endOfPath;
}

//****************************************************
// Beacon search callback functions.
//****************************************************
static int beaconSearchCostToTarget(	AStarSearchData* data,
										Beacon* beaconParent,
										Beacon* beacon,
										BeaconConnection* connectionToBeacon)
{
	Vec3 sourcePos;
	
	// Create the source position.

	vecX(sourcePos) = vecX(beacon->pos);
	vecZ(sourcePos) = vecZ(beacon->pos);
	
	if(pathFindEntity.canFly){
		if(beaconParent){
			vecY(sourcePos) = beaconParent->userFloat;
		}else{
			vecY(sourcePos) = beacon->userFloat;
		}
	}else{
		vecY(sourcePos) = vecY(beacon->pos);
	}

	// Get the positions in range of the connection.
	return DIST_TO_COST_SCALE * distance3(sourcePos, data->targetPos);
}

static int beaconCmpDestYawUncached(const Beacon *b, const BeaconConnection **left, const BeaconConnection **right)
{
	F32 left_yaw, right_yaw;
	Vec3 diff;
	subVec3((*left)->destBeacon->pos, b->pos, diff);
	left_yaw = getVec3Yaw(diff);
	subVec3((*right)->destBeacon->pos, b->pos, diff);
	right_yaw = getVec3Yaw(diff);

	return left_yaw > right_yaw ? -1 : (left_yaw < right_yaw ? 1 : 0);
}

static S32 beaconSearchCostFlight(AStarSearchData* data,
								  Beacon* prevBeacon,
								  BeaconConnection* connFromPrev,
								  Beacon* sourceBeacon,
								  BeaconConnection* conn)
{
	S32 searchDistance;
	Beacon* targetBeacon = conn->destBeacon;
	F32		top_y;
	F32		bottom_y;
	F32		distance;
	Vec3 	sourcePos;
	Vec3 	targetPos;
	Vec3 	sourceInRangePos;
	Vec3 	targetInRangePos;

	bottom_y = vecY(sourceBeacon->pos) + conn->minHeight;
	top_y = vecY(sourceBeacon->pos) + conn->maxHeight;

	if(pathFindEntity.turnRate>0)
	{
		F32 connMax;
		F32 connMin;

		// Don't try to use through tight connections if you're turn limited
		if(conn->maxHeight - conn->minHeight < 15)
			return INT_MAX;


		// Connection height is guaranteed to be at least 15 because of the above
		bottom_y += 5;
		top_y -= 5;

		if(connFromPrev)
		{
			F32 prevTop = vecY(prevBeacon->pos) + connFromPrev->maxHeight - 5;
			F32 prevBottom = vecY(prevBeacon->pos) + connFromPrev->minHeight + 5;

			connMax = MIN(prevTop, top_y);
			connMin = MAX(prevBottom, bottom_y);

			if(connMax - connMin < 5)
			{
				// Don't use tight connection exchanges
				return INT_MAX;
			}

			distance = distance3XZ(prevBeacon->pos, sourceBeacon->pos);

			connMax = MIN(connMax, prevBeacon->userFloat+distance*0.9/2);
			connMin = MAX(connMin, prevBeacon->userFloat-distance*0.9/2);

			if(connMax - connMin < 0)
			{
				// Make sure we can approach the interchange point from where we are
				return INT_MAX;
			}
		}
		else
		{
			// First beacon
			if(sourceBeacon->userFloat < bottom_y || sourceBeacon->userFloat > top_y)
			{
				// Make sure userFloat (start height) is part of the conn
				return INT_MAX;
			}
		}
	}

	setVec3(sourcePos,
		vecX(sourceBeacon->pos),
		sourceBeacon->userFloat,
		vecZ(sourceBeacon->pos));

	vecX(targetPos) = vecX(targetBeacon->pos);
	vecZ(targetPos) = vecZ(targetBeacon->pos);

	if(targetBeacon == data->targetNode){
		vecY(targetPos) = targetBeacon->userFloat;
	}else{
		vecY(targetPos) = CLAMPF32(vecY(sourcePos), bottom_y, top_y);
	}

	setVec3(sourceInRangePos,
		vecX(sourcePos),
		CLAMPF32(vecY(sourcePos), bottom_y, top_y),
		vecZ(sourcePos));

	setVec3(targetInRangePos,
		vecX(targetPos),
		CLAMPF32(vecY(targetPos), bottom_y, top_y),
		vecZ(targetPos));

	distance = distance3(sourceInRangePos, targetInRangePos);
	searchDistance = DIST_TO_COST_SCALE * (	fabs(vecY(targetPos) - vecY(targetInRangePos)) +
		fabs(vecY(sourcePos) - vecY(sourceInRangePos)) +
		distance );

	if(pathFindEntity.turnRate && prevBeacon && connFromPrev)
	{
		// Apply a penalty for sharp turns (not scaled to turn rate just yet)
		Vec3 prevToSrc, srcToDest;
		F32 dot;

		subVec3(sourceBeacon->pos, prevBeacon->pos, prevToSrc);
		subVec3(conn->destBeacon->pos, sourceBeacon->pos, srcToDest);

		normalVec3(prevToSrc);
		normalVec3(srcToDest);

		dot = 2 - dotVec3(prevToSrc, srcToDest);	// Scale it from [-1,1]->[3,1]
		dot = dot*dot*dot - 1;						// Cube it, but subtract 1 bias (meaning dot = 0 if dot was 1)

		searchDistance += DIST_TO_COST_SCALE * dot * distance;
	}

	// Note: source beacon should already be verified as safe
	if(aiShouldAvoidBeacon(pathFindEntity.entref, targetBeacon, conn->minHeight) || 
		aiShouldAvoidLine(pathFindEntity.entref, sourceBeacon, NULL, targetBeacon, conn->minHeight, NULL))
	{
		if(pathFindEntity.useAvoid)
		{
			searchDistance *= 50;
		}
		else
		{
			return INT_MAX;
		}
	}
	

	return searchDistance;
}

static int beaconSearchCostJump(AStarSearchData* data,
								Beacon* prevBeacon,
								BeaconConnection* connFromPrev,
								Beacon* sourceBeacon,
								BeaconConnection* conn)
{
	S32 searchDistance;
	Beacon* targetBeacon = conn->destBeacon;
	F32 minHeight = conn->minHeight + sourceBeacon->floorDistance;

	if(!pathFindEntity.canFly &&
		distance3SquaredXZ(sourceBeacon->pos, conn->destBeacon->pos) > pathFindEntity.maxJumpXZDiffSQR)
	{
		return INT_MAX;
	}
	
	searchDistance =	pathFindEntity.jumpCostInFeet +
						(int)(minHeight * pathFindEntity.jumpHeightCostMult) +
						pathFindEntity.jumpDistCostMult * conn->distance;

	searchDistance *= DIST_TO_COST_SCALE;

	// Note, source beacon will already be verified as safe
	if(	aiShouldAvoidBeacon(pathFindEntity.entref, targetBeacon, conn->minHeight) || 
		aiShouldAvoidLine(pathFindEntity.entref, sourceBeacon, NULL, targetBeacon, conn->minHeight, NULL))
	{
		if(pathFindEntity.useAvoid)
		{
			searchDistance *= 50;
		}
		else
		{
			return INT_MAX;
		}
	}

	return searchDistance;
}

static int beaconSearchCost(AStarSearchData* data,
							Beacon* prevBeacon,
							BeaconConnection* connFromPrev,
							Beacon* sourceBeacon,
							BeaconConnection* conn)
{
	Beacon* targetBeacon = conn->destBeacon;
	U32 timeWhenIWasBad = 0;
	S32 searchDistance;

	if(conn->wasBadEver)
		timeWhenIWasBad = beaconConnectionGetTimeBad(conn);

	if(pathFindEntity.pathLimited && !targetBeacon->pathLimitedAllowed)
		return INT_MAX;
	
	if(pathFindEntity.alwaysFly){
		// Find the distance from the flying position on the source beacon.
		return beaconSearchCostFlight(data, prevBeacon, connFromPrev, sourceBeacon, conn);
	}
	else if(conn->minHeight){
		// Need to jump for this connection.
		return beaconSearchCostJump(data, prevBeacon, connFromPrev, sourceBeacon, conn);		
	}
	else{
		// This is a ground connection.
		searchDistance = conn->distance * DIST_TO_COST_SCALE;

		// Check for avoid.  Source beacon will already be verified as safe if it exists.)
		if(	aiShouldAvoidBeacon(pathFindEntity.entref, targetBeacon, 0) || 
			sourceBeacon &&
			aiShouldAvoidLine(pathFindEntity.entref, sourceBeacon, NULL, targetBeacon, 0, NULL))
		{
			if(pathFindEntity.useAvoid)
			{
				searchDistance *= 50;
			}
			else
			{
				return INT_MAX;
			}
		}
	}
		
	// If I was a bad connection in the last x seconds, factor that in.
	
	if(timeWhenIWasBad){
		U32 timeSinceBad = ABS_TIME_SINCE(timeWhenIWasBad);
		
		if(timeSinceBad < 3000 * SEC_TO_ABS_TIME(5)){
			int inverse = 3000 * SEC_TO_ABS_TIME(5) - timeSinceBad;
			
			searchDistance += inverse * 10 * DIST_TO_COST_SCALE;
		}
	}
	
	return searchDistance;
}

int cmpDynToConn(const BeaconDynamicConnection *dynConn, const BeaconConnection *conn)
{
	return dynConn->conn==conn;
}

static int beaconDefaultFilter(Beacon* sourceBeacon, BeaconConnection *conn, int ground)
{
	Beacon* targetBeacon = conn->destBeacon;
	BeaconPartitionData *sourcePartition = eaGet(&sourceBeacon->partitions, pathFindEntity.partition);
	BeaconPartitionData *targetPartition = beaconGetPartitionData(targetBeacon, pathFindEntity.partition, false);

	if(ground)
	{
		if(pathFindEntity.alwaysFly)
			return true;

		if(eaFindCmp(&sourcePartition->disabledConns, conn, cmpDynToConn)!=-1)
			return true;

		if(targetPartition->block->searchInstance != beacon_state.beaconSearchInstance)
			return true;

		if(pathFindEntity.height && beaconGetCeilingDistance(pathFindEntity.partition, targetBeacon)<pathFindEntity.height)
			return true;

		// Only ever set on beacon server during optional pruning
		if(conn->gflags.optional) 
			return true;
	}
	else
	{
		F32 fBeaconVerticalDist = targetBeacon->pos[1] - sourceBeacon->pos[1];
		if(targetPartition->block->searchInstance != beacon_state.beaconSearchInstance)
			return true;

		if(pathFindEntity.noRaised)
			return true;

		if(eaFindCmp(&sourcePartition->disabledConns, conn, cmpDynToConn)!=-1)
			return true;

		if(!pathFindEntity.canFly)
		{
			if(fBeaconVerticalDist > 0 && pathFindEntity.maxJumpHeight < fBeaconVerticalDist)
				return true;

			if(pathFindEntity.maxJumpXZDiffSQR < distance3SquaredXZ(sourceBeacon->pos, targetBeacon->pos))
				return true;
		}

		if(pathFindEntity.height && (conn->maxHeight-conn->minHeight)<pathFindEntity.height)
			return true;
	}

	return false;
}

static int beaconFilterConnections( AStarSearchData* data,
									Beacon* sourceBeacon,
									BeaconConnection*** connBuffer,
									Beacon*** nodeBuffer,
									int* position,
									int* count,
									BeaconConnectionFilter filter)
{
	BeaconConnection** connections = *connBuffer;
	Beacon** beacons = *nodeBuffer;
	BeaconBlock* block;
	int start = *position;
	void** storage;
	int size;
	int i;
	int connCount;
	int max = *count;
	BeaconPartitionData *sourcePartition = eaGet(&sourceBeacon->partitions, pathFindEntity.partition);

	// connCount is the count of returned connections.

	connCount = 0;

	block = sourcePartition->block;

	// Return ground connections sub-array if still in that range.

	if(start < sourceBeacon->gbConns.size){
		size = sourceBeacon->gbConns.size;
		storage = sourceBeacon->gbConns.storage;

		for(i = start; i < size; i++){
			BeaconConnection* conn = storage[i];
			Beacon* targetBeacon = conn->destBeacon;
			BeaconPartitionData *targetPartition = eaGet(&sourceBeacon->partitions, pathFindEntity.partition);

			if(filter(sourceBeacon, conn, true))
				continue;

			connections[connCount] = conn;
			beacons[connCount] = targetBeacon;

			if(++connCount == max){
				break;
			}
		}

		if(connCount){
			*position = i;
			*count = connCount;

			return 1;
		}

		start = 0;
	}else{
		start -= sourceBeacon->gbConns.size;
	}

	// Return raised connections sub-array if still in that range.

	if(start < sourceBeacon->rbConns.size){
		size = sourceBeacon->rbConns.size;
		storage = sourceBeacon->rbConns.storage;

		for(i = start; i < size; i++){
			BeaconConnection* conn = storage[i];
			Beacon* targetBeacon = conn->destBeacon;

			if(filter(sourceBeacon, conn, false))
				continue;

			connections[connCount] = conn;
			beacons[connCount] = targetBeacon;

			if(++connCount == max)
				break;
		}

		if(connCount)
		{
			*position = sourceBeacon->gbConns.size + i;
			*count = connCount;

			return 1;
		}
	}

	return 0;
}

static int beaconSearchGetConnections(	AStarSearchData* data,
										Beacon* sourceBeacon,
										BeaconConnection*** connBuffer,
										Beacon*** nodeBuffer,
										int* position,
										int* count)
{
	return beaconFilterConnections(data, sourceBeacon, connBuffer, nodeBuffer, position, count, beaconDefaultFilter);
}

static int beaconInBlockFilter(Beacon *sourceBeacon, BeaconConnection *conn, int ground)
{
	Beacon* targetBeacon = conn->destBeacon;
	BeaconPartitionData *sourcePartition = eaGet(&sourceBeacon->partitions, pathFindEntity.partition);
	BeaconPartitionData *targetPartition = beaconGetPartitionData(targetBeacon, pathFindEntity.partition, false);

	if(beaconDefaultFilter(sourceBeacon, conn, ground))
		return 1;

	if(sourcePartition->block->parentBlock != targetPartition->block->parentBlock)
		return 1;

	return 0;
}

static int beaconSearchGetConnectionsInBlock(	AStarSearchData* data,
												Beacon* sourceBeacon,
												BeaconConnection*** connBuffer,
												Beacon*** nodeBuffer,
												int* position,
												int* count)
{
	return beaconFilterConnections(data, sourceBeacon, connBuffer, nodeBuffer, position, count, beaconInBlockFilter);
}

static void beaconSearchOutputPath(	AStarSearchData* data,
									AStarInfo* tailInfo)
{
	BeaconQueuedPathfind*	qpf = data->userData;
	AStarInfo*				curInfo = tailInfo;
	BeaconConnection*		connFromMe = NULL;
	Beacon*					destBeacon = qpf->targetBeacon;
	S32						first = 1;
	F32						last_y;
	
	while(curInfo)
	{
		AStarInfo*			parentInfo;
		NavPathWaypoint*	wp;
		Beacon*				beacon;
		BeaconConnection*	connToMe;
		Beacon*				parentBeacon;
		F32 				bottom_y;
		F32 				top_y;
		F32 				beacon_y;
		S32					makeWaypoint = 1;
		
		parentInfo = curInfo->parentInfo;

		beacon = curInfo->ownerNode;
		connToMe = curInfo->connFromPrevNode;
				
		beacon_y = pathFindEntity.canFly ? beacon->userFloat : vecY(beacon->pos);
		
		if(pathFindEntity.canFly)
		{
			// Create the destination waypoint.
			
			if(beacon == destBeacon && fabs(beacon_y-qpf->destinationHeight) > 3){
				wp = createNavPathWaypoint();
				
				wp->beacon = beacon;
				
				wp->connectType = NAVPATH_CONNECT_FLY;

				vecX(wp->pos) = vecX(beacon->pos);
				vecY(wp->pos) = qpf->destinationHeight+2.0f;		// Positions for waypoints are standard 2ft off the ground (there are -2 ft checks elsewhere)
				vecZ(wp->pos) = vecZ(beacon->pos);
				
				last_y = vecY(wp->pos);
				
				navPathAddHead(&qpf->path, wp);
			}
			else if(beacon == destBeacon)
				last_y = beacon_y;
			
			// Add the waypoint that's in range of connToMe.

			if(connToMe && connToMe->minHeight){
				parentBeacon = parentInfo->ownerNode;
				
				bottom_y = vecY(parentBeacon->pos) + connToMe->minHeight;
				top_y = vecY(parentBeacon->pos) + connToMe->maxHeight;
				
				if(last_y >= bottom_y && last_y <= top_y){
					makeWaypoint = 0;
				}
			}
		}
		
		if(makeWaypoint)
		{
			wp = createNavPathWaypoint();

			wp->beacon = beacon;
			wp->connectionToMe = connToMe;
			
			vecX(wp->pos) = vecX(beacon->pos);
			vecY(wp->pos) = beacon_y;
			vecZ(wp->pos) = vecZ(beacon->pos);
			
			if(connToMe && connToMe->minHeight){
				if(	pathFindEntity.alwaysFly ||
					pathFindEntity.canFly && 
					 (connToMe->minHeight+beaconGetFloorDistance(pathFindEntity.partition, beacon) > pathFindEntity.maxJumpHeight ||
					  SQR(connToMe->distance) > pathFindEntity.maxJumpXZDiffSQR))
				{
					wp->connectType = NAVPATH_CONNECT_FLY;
				}else{
					wp->connectType = NAVPATH_CONNECT_JUMP;

					vecY(wp->pos) -= beaconGetFloorDistance(pathFindEntity.partition, beacon);
					vecY(wp->pos) += 2;		// Positions for waypoints are standard 2ft off the ground (there are -2 ft checks elsewhere)
				}
			}
			else
			{
				if(	pathFindEntity.alwaysFly || 
					pathFindEntity.canFly && beacon->userFloat - vecY(beacon->pos) > 5)
				{
					assert(!connToMe || connToMe->minHeight > 0);
					wp->connectType = NAVPATH_CONNECT_FLY;
				}else{
					wp->connectType = NAVPATH_CONNECT_GROUND;
					
					vecY(wp->pos) = vecY(beacon->pos) - beaconGetFloorDistance(pathFindEntity.partition, beacon);
					vecY(wp->pos) += 2; 		// Positions for waypoints are standard 2ft off the ground (there are -2 ft checks elsewhere)
				}
			}
			
			navPathAddHead(&qpf->path, wp);
		}
		
		// Add the waypoint over parent beacon that's clamped to connToMe, if necessary.

		if(pathFindEntity.canFly)
		{
			if(connToMe && connToMe->minHeight){
				beacon_y = parentBeacon->userFloat;
				
				bottom_y = vecY(parentBeacon->pos) + connToMe->minHeight;
				top_y = vecY(parentBeacon->pos) + connToMe->maxHeight;
				
				wp = createNavPathWaypoint();
				
				wp->beacon = parentBeacon;
				
				wp->connectType = NAVPATH_CONNECT_FLY;
				
				vecX(wp->pos) = vecX(parentBeacon->pos);
				vecY(wp->pos) = CLAMPF32(beacon_y, bottom_y, top_y)+2.0f;		// Positions for waypoints are standard 2ft off the ground (there are -2 ft checks elsewhere)
				vecZ(wp->pos) = vecZ(parentBeacon->pos);
				
				last_y = vecY(wp->pos);

				navPathAddHead(&qpf->path, wp);
			}
			else if(parentInfo){
				BeaconConnection* connToParent = parentInfo->connFromPrevNode;
				
				if(connToParent && connToParent->minHeight){
					parentBeacon = parentInfo->ownerNode;

					wp = createNavPathWaypoint();
					
					wp->beacon = parentBeacon;
					wp->connectionToMe = connToParent;
					
					wp->connectType = NAVPATH_CONNECT_FLY;
					
					vecX(wp->pos) = vecX(parentBeacon->pos);
					vecY(wp->pos) = vecY(parentBeacon->pos)+2.0f;		// Positions for waypoints are standard 2ft off the ground (there are -2 ft checks elsewhere)
					vecZ(wp->pos) = vecZ(parentBeacon->pos);
					
					last_y = vecY(wp->pos);

					navPathAddHead(&qpf->path, wp);
				}
			}
		}
		
		// And go to the parent node.
		
		first = 0;
				
		curInfo = parentInfo;
	}
	
	qpf->path.curWaypoint = 0;
}

static void beaconSearchCloseNode(	AStarSearchData* data,
									Beacon* parentBeacon,
									BeaconConnection* conn,
									Beacon* closedBeacon)
{
	F32 top_y, bottom_y;
	F32 cur_y;
	F32 beacon_y;

	assert(beacon_state.mainThreadId==GetCurrentThreadId());
	
	if(!parentBeacon)
		return;
		
	if(!conn->minHeight){
		closedBeacon->userFloat = vecY(closedBeacon->pos);
		return;
	}
		
	cur_y = parentBeacon->userFloat;
	
	beacon_y = vecY(parentBeacon->pos);
	
	bottom_y = beacon_y + conn->minHeight;
	top_y = beacon_y + conn->maxHeight;//- MINHEIGHT;
	
	if(pathFindEntity.turnRate>0)
	{
		Beacon *targetBeacon = data->targetNode;
		F32 dist = distance3XZ(parentBeacon->pos, closedBeacon->pos);

		bottom_y += 5;
		top_y -= 5;

		// Since we're turn rate limited, we want to approach the target y!
		closedBeacon->userFloat = CLAMPF32(targetBeacon->userFloat, bottom_y, top_y);
		// But also because of the turn rate, we can't go more than 45 degrees (or about half the XZ dist, but only 90% here for safety)
		closedBeacon->userFloat = CLAMPF32(closedBeacon->userFloat, cur_y-dist*0.9/2, cur_y+dist*0.9/2);
	}
	else
	{
		closedBeacon->userFloat = CLAMPF32(cur_y, bottom_y, top_y);
	}
}

static const char* beaconSearchGetNodeInfo(	Beacon* beacon,
											Beacon* parentBeacon,
											BeaconConnection* conn)
{
	static char buffer[1000];

	if(parentBeacon){
		sprintf(buffer,
				"(%1.f, %1.f, %1.f) --> (%1.f, %1.f, %1.f)\t[%d,%d]",
				vecX(parentBeacon->pos), parentBeacon->userFloat, vecZ(parentBeacon->pos),
				vecX(beacon->pos), beacon->userFloat, vecZ(beacon->pos),
				conn->minHeight,
				conn->maxHeight);
	}else{
		sprintf(buffer, "(%1.f, %1.f, %1.f)", vecX(beacon->pos), beacon->userFloat, vecZ(beacon->pos));
	}
	
	return buffer;
}

void beaconSearchReportClosedInfo(	AStarInfo* closedInfo,
									int checkedConnectionCount)
{
	Beacon* beacon = closedInfo->ownerNode;
	Packet* pak = staticDebugPacket;
	int i;
	
	// New set.
	
	pktSendBits(pak, 1, 1);
	
	pktSendBitsPack(pak, 10, checkedConnectionCount);
	pktSendBitsPack(pak, 10, closedInfo->costSoFar);
	pktSendBitsPack(pak, 10, closedInfo->totalCost);
	
	for(i = 0; i < beacon->gbConns.size; i++){
		BeaconConnection* conn = beacon->gbConns.storage[i];
		Beacon* b = conn->destBeacon;
		AStarInfo* info = b->astarInfo;
		
		pktSendBits(pak, 1, 1);

		if(info){
			pktSendBits(pak, 1, 1);
			pktSendBitsPack(pak, 8, info->queueIndex);
			pktSendBitsPack(pak, 24, info->totalCost);
			pktSendBitsPack(pak, 24, info->costSoFar);
		}else{
			pktSendBits(pak, 1, 0);
		}

		pktSendF32(pak, vecX(b->pos));
		pktSendF32(pak, vecY(b->pos));
		pktSendF32(pak, vecZ(b->pos));
	}

	for(i = 0; i < beacon->rbConns.size; i++){
		BeaconConnection* conn = beacon->rbConns.storage[i];
		Beacon* b = conn->destBeacon;
		AStarInfo* info = b->astarInfo;
		
		pktSendBits(pak, 1, 1);
		
		if(info){
			pktSendBits(pak, 1, 1);
			pktSendBitsPack(pak, 8, info->queueIndex);
			pktSendBitsPack(pak, 24, info->totalCost);
			pktSendBitsPack(pak, 24, info->costSoFar);
		}else{
			pktSendBits(pak, 1, 0);
		}

		pktSendF32(pak, vecX(b->pos));
		pktSendF32(pak, vecY(b->pos));
		pktSendF32(pak, vecZ(b->pos));
	}

	pktSendBits(pak, 1, 0);

	while(closedInfo){
		Beacon* b = closedInfo->ownerNode;
		BeaconConnection* conn = closedInfo->connFromPrevNode;
		
		pktSendBits(pak, 1, 1);

		pktSendF32(pak, vecX(b->pos));
		pktSendF32(pak, vecY(b->pos));
		pktSendF32(pak, vecZ(b->pos));
		pktSendF32(pak, pathFindEntity.canFly ? b->userFloat : 0);
		
		if(conn){
			pktSendBits(pak, 1, 1);
			
			pktSendF32(pak, conn->minHeight);
			pktSendF32(pak, conn->maxHeight);
		}else{
			pktSendBits(pak, 1, 0);
		}
		
		closedInfo = closedInfo->parentInfo;
	}

	pktSendBits(pak, 1, 0);
}

//****************************************************
// BeaconBlock search callback functions.
//****************************************************

static struct {
	BeaconBlock* sourceBlock;
	BeaconBlock* targetBlock;
} blockSearch;

static int beaconBlockSearchCostToTarget(	AStarSearchData* data,
											BeaconBlock* blockParent,
											BeaconBlock* block,
											BeaconBlockConnection* connectionToBlock)
{
	if(pathFindEntity.canFly){
		Vec3 blockPos;
		
		blockPos[0] = vecX(block->pos);
		blockPos[2] = vecZ(block->pos);
		
		if(blockParent){
			F32 minY = vecY(blockParent->pos) + connectionToBlock->minHeight;
			F32 maxY = vecY(blockParent->pos) + connectionToBlock->maxHeight;
			
			blockPos[1] = CLAMPF32(blockParent->searchY, minY, maxY);
		}else{
			blockPos[1] = block->searchY;
		}
		
		return distance3(blockPos, data->targetPos);
	}else{
		Vec3 blockPos;
		copyVec3(block->pos, blockPos);
		return distance3(blockPos, data->targetPos);
	}
}

static int beaconBlockSearchCost(	AStarSearchData* data,
									BeaconBlock* prevBlock,
									BeaconBlockConnection* connFromPrev,
									BeaconBlock* sourceBlock,
									BeaconBlockConnection* conn)
{
	BeaconBlock* targetBlock = conn->destBlock;
	
	if(pathFindEntity.canFly){
		Vec3 sourcePos = {vecX(sourceBlock->pos), sourceBlock->searchY, vecZ(sourceBlock->pos)};
		F32 minY = vecY(sourceBlock->pos) + conn->minHeight;
		F32 maxY = vecY(sourceBlock->pos) + conn->maxHeight;
		Vec3 targetPos = {vecX(targetBlock->pos), CLAMPF32(vecY(sourcePos), minY, maxY), vecZ(targetBlock->pos)};
		
		return distance3(sourcePos, targetPos);
	}else{
		Vec3 srcPos;
		Vec3 dstPos;

		if(targetBlock == blockSearch.targetBlock){
			copyVec3(data->targetPos, dstPos);
		}else{
			copyVec3(targetBlock->pos, dstPos);
		}
		
		if(sourceBlock == blockSearch.sourceBlock){
			copyVec3(data->sourcePos, srcPos);
		}else{
			copyVec3(sourceBlock->pos, srcPos);
		}
		
		return distance3(srcPos, dstPos);
	}
}

static int beaconBlockConnectionDefaultFilter(BeaconBlock *block, BeaconBlockConnection *blockConn, int ground)
{
	if(blockConn->blockCount >= blockConn->connCount)
		return true;

	if(!ground)
	{
		if(pathFindEntity.entref && !pathFindEntity.canFly &&
			pathFindEntity.maxJumpHeight < blockConn->minJumpHeight)
		{
			// Can't make jump height
			return true;
		}
	}

	return false;
}

static int beaconBlockConnectionInGalaxyFilter(BeaconBlock *block, BeaconBlockConnection *blockConn, int ground)
{
	if(beaconBlockConnectionDefaultFilter(block, blockConn, ground))
		return true;

	if(block->galaxy != blockConn->destBlock->galaxy)
		return true;

	return false;
}

static int beaconBlockFilterConnections(AStarSearchData* data,
										BeaconBlock* subBlock,
										BeaconBlockConnection*** connBuffer,
										BeaconBlock*** nodeBuffer,
										int* position,
										int* count,
										BlockConnectionFilter filter)
{
	int start = *position;
	int max = *count;
	BeaconBlockConnection** connections = *connBuffer;
	BeaconBlock** blocks = *nodeBuffer;
	int i;
	int j;

	// Return ground connections sub-array if still in that range.

	if(start < subBlock->gbbConns.size)
	{
		for(i=start, j=0; j<max && i<subBlock->gbbConns.size; i++)
		{
			BeaconBlockConnection *blockConn = subBlock->gbbConns.storage[start + i];
			BeaconBlock *dst = blockConn->destBlock;

			(*position)++;

			if(filter(subBlock, blockConn, true))
				continue;

			(*connBuffer)[j] = subBlock->gbbConns.storage[start + i];
			(*nodeBuffer)[j] = dst;
			j++;
			
			if(j == max)
				break;
		}

		if(j > 0)
		{
			*count = j;
			return 1;
		}
	}
	else
		start -= subBlock->gbbConns.size;

	// Return raised connections sub-array if still in that range.

	if(start < subBlock->rbbConns.size){
		int size = subBlock->rbbConns.size;

		for(i = start, j = 0; i < size; i++)
		{
			BeaconBlockConnection* conn = subBlock->rbbConns.storage[i];

			(*position)++;

			if(filter(subBlock, conn, false))
				continue;

			connections[j] = conn;
			blocks[j] = conn->destBlock;
			j++;

			if(j == max)
				break;
		}

		if(j > 0)
		{
			*count = j;
			return 1;
		}
	}

	return 0;
}

static int beaconBlockSearchGetConnectionsInGalaxy(AStarSearchData* data,
											BeaconBlock* subBlock,
											BeaconBlockConnection*** connBuffer,
											BeaconBlock*** nodeBuffer,
											int* position,
											int* count)
{
	return beaconBlockFilterConnections(data, 
										subBlock, 
										connBuffer, 
										nodeBuffer, 
										position, 
										count, 
										beaconBlockConnectionInGalaxyFilter);
}

static int beaconBlockSearchGetConnections(	AStarSearchData* data,
											BeaconBlock* subBlock,
											BeaconBlockConnection*** connBuffer,
											BeaconBlock*** nodeBuffer,
											int* position,
											int* count)
{
	return beaconBlockFilterConnections(data,
										subBlock,
										connBuffer,
										nodeBuffer,
										position, 
										count,
										beaconBlockConnectionDefaultFilter);
}

static void beaconBlockSearchOutputPath(	AStarSearchData* data,
											AStarInfo* tailInfo)
{
	AStarInfo* blockTail;
		
	// Now there's a block path, determine which block each previous block should be heading for.
	// Do this by going through the block path until arriving at a block that is the opposite increment
	// from the previous block, meaning that if the previous block is FARTHER away from the destination
	// than two blocks back is, AND that the current block is CLOSER than the previous block to the
	// destination, then we've found a turning point in the path.
	
	// Set the search instance for all the valid blocks.
	
	beacon_state.beaconSearchInstance++;

	staticAvailableBeaconCount = 0;
	
	if(staticDebugPacket){
		Packet* pak = staticDebugPacket;
		
		for(blockTail = tailInfo; blockTail; blockTail = blockTail->parentInfo){
			BeaconBlock* block = blockTail->ownerNode;
			int i;
			
			pktSendBits(pak, 1, 1);
			
			pktSendF32(pak, block->pos[0]);
			pktSendF32(pak, block->pos[1]);
			pktSendF32(pak, block->pos[2]);
			
			pktSendF32(pak, block->searchY);
			
			pktSendBitsPack(pak, 8, block->beaconArray.size);
			
			for(i = 0; i < block->beaconArray.size; i++){
				Beacon* b = block->beaconArray.storage[i];

				pktSendF32(pak, vecX(b->pos));
				pktSendF32(pak, vecY(b->pos));
				pktSendF32(pak, vecZ(b->pos));
			}
		}

		pktSendBits(pak, 1, 0);
	}

	PERFINFO_AUTO_START("Mark Blocks", 1);
		for(blockTail = tailInfo; blockTail; blockTail = blockTail->parentInfo){
			BeaconBlock* block = blockTail->ownerNode;
			int i;
			int connCount;
			BeaconBlockConnection** conns;
			
			if(block->searchInstance != beacon_state.beaconSearchInstance){
				staticAvailableBeaconCount += block->beaconArray.size;
				block->searchInstance = beacon_state.beaconSearchInstance;
			}
			
			// Set the search instance for all the neighbor blocks to give the path that fuzzy feeling.
			
			connCount = block->gbbConns.size;
			conns = (BeaconBlockConnection**)block->gbbConns.storage;
			
			for(i = 0; i < connCount; i++){
				BeaconBlockConnection* conn = conns[i];

				if(conn->destBlock->searchInstance != beacon_state.beaconSearchInstance){
					staticAvailableBeaconCount += conn->destBlock->beaconArray.size;
					conn->destBlock->searchInstance = beacon_state.beaconSearchInstance;
				}
			}
			
			connCount = block->rbbConns.size;
			conns = (BeaconBlockConnection**)block->rbbConns.storage;

			for(i = 0; i < connCount; i++){
				BeaconBlockConnection* conn = conns[i];

				if(conn->destBlock->searchInstance != beacon_state.beaconSearchInstance){
					staticAvailableBeaconCount += conn->destBlock->beaconArray.size;
					conn->destBlock->searchInstance = beacon_state.beaconSearchInstance;
				}
			}
		}
	PERFINFO_AUTO_STOP();
}

static void beaconBlockSearchCloseNode(	AStarSearchData* data,
										BeaconBlock* parentBlock,
										BeaconBlockConnection* conn,
										BeaconBlock* closedBlock)
{
	F32 curY;
	F32 minY;
	F32 maxY;
	
	if(parentBlock){
		curY = parentBlock->searchY;
		minY = vecY(parentBlock->pos) + conn->minHeight;
		maxY = vecY(parentBlock->pos) + conn->maxHeight;

		closedBlock->searchY = CLAMPF32(curY, minY, maxY);
	}
}

//****************************************************
// Beacon galaxy search callback functions.
//****************************************************

static int beaconGalaxySearchCostToTarget(	AStarSearchData* data,
											BeaconBlock* galaxyParent,
											BeaconBlock* galaxy,
											BeaconBlockConnection* connectionToGalaxy)
{
	if(blockSearch.targetBlock == galaxy)
		return 0;
	
	return 1;
}

static int beaconGalaxySearchCost(	AStarSearchData* data,
									BeaconBlock* prevGalaxy,
									BeaconBlockConnection* connFromPrevGalaxy,
									BeaconBlock* sourceGalaxy,
									BeaconBlockConnection* conn)
{
	BeaconBlock* destGalaxy = conn->destBlock;

	if(blockSearch.targetBlock == destGalaxy)
		return 0;
	
	return 1;
}

static int beaconGalaxySearchGetConnections(AStarSearchData* data,
											BeaconBlock* galaxy,
											BeaconBlockConnection*** connBuffer,
											BeaconBlock*** nodeBuffer,
											int* position,
											int* count)
{
	int start = *position;
	BeaconBlockConnection** connections = *connBuffer;
	BeaconBlock** destBlocks = *nodeBuffer;
	int i;
	int connCount;

	// Return ground connections sub-array if still in that range.
	
	if(start < galaxy->gbbConns.size){
		if(*count > galaxy->gbbConns.size - start){
			*count = galaxy->gbbConns.size - start;
		}
		
		*position += *count;
		
		*connBuffer = (BeaconBlockConnection**)galaxy->gbbConns.storage + start;
		connections = *connBuffer;
		
		connCount = *count;
		
		for(i = 0; i < connCount; i++){
			destBlocks[i] = connections[i]->destBlock;
		}
		
		return 1;
	}

	// Return raised connections sub-array if still in that range.

	if(pathFindEntity.maxJumpHeight > 0){
		start -= galaxy->gbbConns.size;
		
		if(start < galaxy->rbbConns.size){
			int size = galaxy->rbbConns.size;
			int max = *count;
			
			if(pathFindEntity.canFly){
				for(i = start, connCount = 0; i < size; i++){
					BeaconBlockConnection* conn = galaxy->rbbConns.storage[i];
					
					// All connections are okay for flying.
					
					connections[connCount] = conn;
					destBlocks[connCount] = conn->destBlock;
					
					if(++connCount == max){
						break;
					}
				}
			}else{
				for(i = start, connCount = 0; i < size; i++){
					BeaconBlockConnection* conn = galaxy->rbbConns.storage[i];
					
					// Check if the entity's maxJumpHeight is >= the connection minJumpHeight.
					
					if(pathFindEntity.maxJumpHeight >= conn->minJumpHeight){
						connections[connCount] = conn;
						destBlocks[connCount] = conn->destBlock;
						
						if(++connCount == max){
							break;
						}
					}
				}
			}
			
			if(connCount){
				*count = connCount;
				*position = galaxy->gbbConns.size + i;
				return 1;
			}
		}
	}
		
	return 0;
}

struct {
	// Input
	BeaconBlock*				sourceCluster;
	BeaconBlock*				sourceGalaxy;
	Vec3						sourcePos;
	Beacon*						sourceBeacon;
	U32							inAvoid : 1;				// We don't want to use avoid nodes, but we can't reject regular beacons because we're standing in an avoid
	BeaconDynamicConnection*	dynConnToTarget;
	Vec3						searchSrcPos;
	Vec3						searchDstPos;

	// Output
	U32							wantClusterConnect : 1;		// Target beacon is in a different cluster.
	Beacon*						clusterConnTargetBeacon;	// Beacon on other side of a cluster connection.
} beaconFind;


//****************************************************
// Beacon cluster search callback functions.
//****************************************************

static int beaconClusterSearchCostToTarget(	AStarSearchData* data,
											BeaconBlock* clusterParent,
											BeaconBlock* cluster,
											BeaconClusterConnection* connectionToGalaxy)
{
	if(blockSearch.targetBlock == cluster)
	{
		return distance3(beaconFind.searchDstPos, connectionToGalaxy->target.pos);
		//return 0;
	}
	
	return 1;
}

static int beaconClusterSearchCost(	AStarSearchData* data,
									BeaconBlock* prevCluster,
									BeaconBlockConnection* connFromPrevCluster,
									BeaconBlock* sourceCluster,
									BeaconClusterConnection* conn)
{
	BeaconBlock* targetCluster = conn->dstCluster;

	if(blockSearch.targetBlock == targetCluster)
	{
		return distance3(beaconFind.sourceBeacon->pos, conn->source.pos);
		//return distance3(blockSearch.sourceBlock->pos, targetCluster->pos);
	}
	
	return INT_MAX;
}

static int beaconClusterSearchGetConnections(	AStarSearchData* data,
												BeaconBlock* cluster,
												BeaconClusterConnection*** connBuffer,
												BeaconBlock*** nodeBuffer,
												int* position,
												int* count)
{
	int start = *position;
	BeaconClusterConnection** connections = *connBuffer;
	BeaconBlock** targetClusters = *nodeBuffer;
	int i;
	int connCount;

	if(start < cluster->gbbConns.size){
		if(*count > cluster->gbbConns.size - start){
			*count = cluster->gbbConns.size - start;
		}
		
		*position += *count;
		
		*connBuffer = (BeaconClusterConnection**)cluster->gbbConns.storage + start;
		connections = *connBuffer;
		
		connCount = *count;
		
		for(i = 0; i < connCount; i++){
			targetClusters[i] = connections[i]->dstCluster;
		}
		
		return 1;
	}

	return 0;
}

static struct {
	BeaconClusterConnection* connToNextCluster;
} clusterSearch;

static void beaconClusterSearchOutputPath(	AStarSearchData* data,
											AStarInfo* tailInfo)
{
	while(	tailInfo->parentInfo &&
			tailInfo->parentInfo->ownerNode != blockSearch.sourceBlock)
	{
		tailInfo = tailInfo->parentInfo;
	}

	if(tailInfo->parentInfo && tailInfo->parentInfo->ownerNode == blockSearch.sourceBlock){
		clusterSearch.connToNextCluster = tailInfo->connFromPrevNode;
	}
}

//****************************************************
// Non-astar-callback functions.
//****************************************************

static int beaconVisibleFromGroundPos(int iPartitionIdx, Beacon* b, Vec3 pos){
	Vec3 beaconPos;
	Vec3 pos2;
	WorldCollCollideResults results;
	BeaconPartitionData *bPartition = beaconGetPartitionData(b, pathFindEntity.partition, false);

	if(!bPartition->block)
		return 0;

	copyVec3(b->pos, beaconPos);
	copyVec3(pos, pos2);

	// beacon is already 2 ft off the ground
	//vecY(beaconPos) += 3;
	vecY(pos2) += 2;
	//vecY(pos2) += 3;

	if(distance3Squared(pos2, beaconPos) > SQR(combatBeaconGridBlockSize))
		return 0;
		
	if(beaconRayCollide(iPartitionIdx, worldGetActiveColl(iPartitionIdx), pos2, beaconPos, WC_FILTER_BIT_MOVEMENT, &results))
		return 0;

	if(beaconRayCollide(iPartitionIdx, worldGetActiveColl(iPartitionIdx), beaconPos, pos2, WC_FILTER_BIT_MOVEMENT, &results))
		return 0;
	
	return 1;
}

static void beaconGetClosestAirPos(int iPartitionIdx, Beacon* b, const Vec3 pos, Vec3 posOut){
	float pos_y = vecY(b->pos);
	float top_y = pos_y + beaconGetCeilingDistance(iPartitionIdx, b);
	float bottom_y = pos_y - beaconGetFloorDistance(iPartitionIdx, b) + 0.2;

	if(top_y > pos_y + 1.0)
		top_y -= 1.0;
	
	vecX(posOut) = vecX(b->pos);
	vecY(posOut) = CLAMPF32(vecY(pos), bottom_y, top_y);
	vecZ(posOut) = vecZ(b->pos);
}

static int beaconVisibleFromAirPos(int iPartitionIdx, Beacon* b, Vec3 pos){
	WorldCollCollideResults results = {0};

	if(beaconRayCollide(iPartitionIdx, worldGetActiveColl(iPartitionIdx), b->pos, pos, WC_FILTER_BIT_MOVEMENT, &results))
		return 0;

	if(beaconRayCollide(iPartitionIdx, worldGetActiveColl(iPartitionIdx), pos, b->pos, WC_FILTER_BIT_MOVEMENT, &results))
		return 0;
	
	return 1;
}

static int __cdecl sortBeaconArrayByDistanceHelper(const Beacon** b1, const Beacon** b2){
	if((*b1)->userFloat > (*b2)->userFloat)
		return 1;
	else if((*b1)->userFloat == (*b2)->userFloat)
		return 0;
	else
		return -1;
}

static void sortBeaconArrayByDistance(Array* array, const Vec3 sourcePos, int calcDistances){
	if(calcDistances){
		int i;
		Vec3 diff;
		
		for(i = 0; i < array->size; i++){
			Beacon* beacon = array->storage[i];
			
			subVec3(sourcePos, beacon->pos, diff);
			
			beacon->userFloat = lengthVec3Squared(diff);
		}
	}
		
	PERFINFO_AUTO_START("beaconSort", 1);

	qsort(array->storage, array->size, sizeof(array->storage[0]), sortBeaconArrayByDistanceHelper);
	
	PERFINFO_AUTO_STOP();
}

Array* beaconMakeSortedNearbyBeaconArray(int iPartitionIdx, const Vec3 sourcePos, float searchRadius){
	int min_block[3];
	int max_block[3];
	int x, z;
	BeaconStatePartition *partition = beaconStatePartitionGet(pathFindEntity.partition, false);

	assert(beacon_state.mainThreadId==GetCurrentThreadId());
	
	staticBeaconSortArray.size = 0;

	if(!partition)
		return &staticBeaconSortArray;
	
	min_block[0] = beaconMakeGridBlockCoord(sourcePos[0] - searchRadius);
	max_block[0] = beaconMakeGridBlockCoord(sourcePos[0] + searchRadius);
	min_block[2] = beaconMakeGridBlockCoord(sourcePos[2] - searchRadius);
	max_block[2] = beaconMakeGridBlockCoord(sourcePos[2] + searchRadius);
		
	if(pathFindEntity.canFly){
		// I can fly, so anything in my column of blocks is okay.
		
		min_block[1] = staticBeaconSortMinY;
		max_block[1] = staticBeaconSortMaxY;
	}else{
		// If I can't fly, then stick to beacons that are within 100 feet.
		
		min_block[1] = beaconMakeGridBlockCoord(sourcePos[1] - searchRadius);
		max_block[1] = beaconMakeGridBlockCoord(sourcePos[1] + searchRadius);
	}

	for(x = min_block[0]; x <= max_block[0]; x++){
		for(z = min_block[2]; z <= max_block[2]; z++){
			BeaconBlock* block;
			int y;
			
			for(y = min_block[1]; y <= max_block[1]; y++){
				block = beaconGetGridBlockByCoords(partition, x, y, z, 0);
																		
				// Add all beacons that are within the size of a grid block.
				
				if(block){
					int j;
					
					for(j = 0; j < block->beaconArray.size; j++){
						Beacon* beacon = block->beaconArray.storage[j];
						
						if(!beacon->gbConns.size && !beacon->rbConns.size){
							continue;
						}

						if(pathFindEntity.pathLimited && !beacon->pathLimitedAllowed){
							continue;
						}
						
						if(pathFindEntity.canFly){
							// If I'm flying, then project to the nearest point on the vertical beacon line.
							
							Vec3 pos;

							beaconGetClosestAirPos(iPartitionIdx, beacon, sourcePos, pos);
							
							beacon->userFloat = distance3Squared(pos, sourcePos);
						}else{
							beacon->userFloat = distance3Squared(beacon->pos, sourcePos);
						}
									
						if(beacon->userFloat <= SQR(searchRadius) &&
							staticBeaconSortArray.size < staticBeaconSortArray.maxSize)
						{
							staticBeaconSortArray.storage[staticBeaconSortArray.size++] = beacon;
						}

						if(beacon->pathsBlockedToMe){
							beacon->userFloat *= SQR(beacon->pathsBlockedToMe+1);
						}
					}
				}
			}
		}
	}
	
	sortBeaconArrayByDistance(&staticBeaconSortArray, sourcePos, 0);

	return &staticBeaconSortArray;
}

void beaconGetNearbyBeacons(Beacon ***peaBeacons,
							  const Vec3 sourcePos, 
							  F32 searchRadius, 
							  FilterBeaconFunc callback, 
							  void* userData )
{
	int min_block[3];
	int max_block[3];
	int x, z;
	BeaconStatePartition *partition = beaconStatePartitionGet(pathFindEntity.partition, false);

	if (!peaBeacons)
		return;

	min_block[0] = beaconMakeGridBlockCoord(sourcePos[0] - searchRadius);
	max_block[0] = beaconMakeGridBlockCoord(sourcePos[0] + searchRadius);
	min_block[1] = beaconMakeGridBlockCoord(sourcePos[1] - searchRadius);
	max_block[1] = beaconMakeGridBlockCoord(sourcePos[1] + searchRadius);
	min_block[2] = beaconMakeGridBlockCoord(sourcePos[2] - searchRadius);
	max_block[2] = beaconMakeGridBlockCoord(sourcePos[2] + searchRadius);

	for(x = min_block[0]; x <= max_block[0]; x++) {
		for(z = min_block[2]; z <= max_block[2]; z++) {
			BeaconBlock* block;
			int y;

			for(y = min_block[1]; y <= max_block[1]; y++) {
				block = beaconGetGridBlockByCoords(partition, x, y, z, 0);

				// Add all beacons that are within the size of a grid block.
				if(block) {
					int j;
					F32 fDistSQ;

					for(j = 0; j < block->beaconArray.size; j++) {
						Beacon* beacon = block->beaconArray.storage[j];
						
						fDistSQ = distance3Squared(beacon->pos, sourcePos);

						if(fDistSQ <= SQR(searchRadius)) {
							if (!callback || callback(beacon, userData))
							{
								eaPush(peaBeacons, beacon);	
							}
						}
					}
				}
			}
		}
	}
}

static Beacon* getClusterConnect(	BeaconBlock* sourceCluster,
									BeaconBlock* targetCluster)
{
	AStarSearchData *searchData = beacon_state.astarDataOther;

	NavSearchFunctions clusterSearchFunctions = {
		(NavSearchCostToTargetFunction)		beaconClusterSearchCostToTarget,
		(NavSearchCostFunction)				beaconClusterSearchCost,
		(NavSearchGetConnectionsFunction)	beaconClusterSearchGetConnections,
		(NavSearchOutputPath)				beaconClusterSearchOutputPath,
		(NavSearchCloseNode)				NULL,
		(NavSearchGetNodeInfo)				NULL,
		(NavSearchReportClosedInfo)			NULL,
	};

	if(	!sourceCluster ||
		!targetCluster)
	{
		return NULL;
	}

	searchData->sourceNode = sourceCluster;
	searchData->targetNode = targetCluster;
	searchData->searching = 0;
	searchData->pathWasOutput = 0;
	searchData->steps = 0;
	searchData->nodeAStarInfoOffset = offsetof(BeaconBlock, astarInfo);

	clusterSearch.connToNextCluster = NULL;

	blockSearch.sourceBlock = sourceCluster;
	blockSearch.targetBlock = targetCluster;
	
	beaconFind.clusterConnTargetBeacon = NULL;
	beaconFind.dynConnToTarget = NULL;

	PERFINFO_AUTO_START("A* cluster", 1);
		AStarSearch(searchData, &clusterSearchFunctions);
	PERFINFO_AUTO_STOP();

	if(clusterSearch.connToNextCluster){
		beaconFind.clusterConnTargetBeacon = clusterSearch.connToNextCluster->target.beacon;
		
		beaconFind.dynConnToTarget = clusterSearch.connToNextCluster->dynConnToTarget;
		
		return clusterSearch.connToNextCluster->source.beacon;
	}

	return NULL;
}

static Beacon* checkForClusterConnection(BeaconBlock* targetGalaxy){
	Beacon* result = getClusterConnect(beaconFind.sourceCluster, targetGalaxy->cluster);

	if(	result && 
		beaconGalaxyPathExists(	beaconFind.sourceBeacon,
								result,
								MAX(pathFindEntity.maxJumpHeight,30),
								pathFindEntity.canFly))
	{
		beaconFind.wantClusterConnect = 1;
		return result;
	}

	return NULL;
}

static Beacon* getBestBeaconGround(int iPartitionIdx, const Vec3 startPos, const Vec3 targetPos, int* outFoundLOS){
	int i;
	Beacon* bestRaisedConnBeacon = NULL;
	F32 closestBeaconDist = FLT_MAX;
	Beacon* bestBeacon = NULL;
	int numRaycasts = 0;
	BeaconPartitionData * pStartBeaconPartitionData = NULL;

	F32 sourceTargetDist = targetPos ? distance3XZ(startPos, targetPos) : 0;

	// We need partition data for the starting point so we can check avoids near it, since some target beacons will be far from avoids that
	// are relevant to us
	{
		Beacon * pNearestBeacon = beaconGetNearestBeacon(iPartitionIdx,startPos);
		if (pNearestBeacon)
		{
			pStartBeaconPartitionData = beaconGetPartitionData(pNearestBeacon, iPartitionIdx, false);
		}
	}
	
	for(i = 0; i < staticBeaconSortArray.size && numRaycasts<10; i++){
		Beacon* targetBeacon = staticBeaconSortArray.storage[i];
		BeaconPartitionData *targetPartition;
		BeaconBlock* targetBlock;
		BeaconBlock* targetGalaxy;
		int good;
		int hasRaisedOnly = false;
		F32 beaconDist;

		if(	!targetBeacon
			||
			!targetBeacon->gbConns.size && bestRaisedConnBeacon
			||
			!targetBeacon->gbConns.size && !targetBeacon->rbConns.size)
		{
			continue;
		}

		if(!pathFindEntity.canFly)
		{
			F32 xz = distance3XZ(startPos, targetBeacon->pos);
			F32 y = distanceY(startPos, targetBeacon->pos);

			beaconDist = xz + y * pathFindEntity.noFlyInitialYCostMult;
		}
		else
			beaconDist = distance3(startPos, targetBeacon->pos);

		if(beaconDist > closestBeaconDist)
			break;
		
		if(!targetBeacon->gbConns.size)
		{
			int j;
			int foundUsableRaisedConn = false;

			hasRaisedOnly = true;

			for(j = 0; j < targetBeacon->rbConns.size; j++)
			{
				BeaconConnection* conn = targetBeacon->rbConns.storage[j];
				if(conn->minHeight < pathFindEntity.maxJumpHeight)
				{
					foundUsableRaisedConn = true;
					break;
				}
			}

			if(!foundUsableRaisedConn)
				continue;
		}

		targetPartition = beaconGetPartitionData(targetBeacon, pathFindEntity.partition, false);
		targetBlock = targetPartition->block;
		targetGalaxy = targetBlock ? targetBlock->galaxy : NULL;
		
		// isGoodForSearch is always 0, but searchInstance only gets updated when the
		// galaxy is actually bad, so it works I guess...
		if(targetGalaxy &&
			(targetGalaxy->searchInstance == beacon_state.galaxySearchInstance &&
			!targetGalaxy->isGoodForSearch
			||
			!pathFindEntity.useEnterable && beaconFind.sourceCluster &&
			targetGalaxy->cluster != beaconFind.sourceCluster))
		{
			// This galaxy was already found to be bad, so ignore it.
			
			staticBeaconSortArray.storage[i] = NULL;
			
			continue;
		}
		
		PERFINFO_AUTO_START("ground", 1);
			good = true;
			if(!pathFindEntity.useAvoid && pathFindEntity.entref)
			{ 
				if (aiShouldAvoidBeacon(pathFindEntity.entref, targetBeacon, 0) ||
				 (!beaconFind.inAvoid && aiShouldAvoidLine(pathFindEntity.entref, NULL, startPos, targetBeacon, 0, pStartBeaconPartitionData)))
				{
					staticBeaconSortArray.storage[i] = NULL;
					good = false;
				}
			}

			if(!good)
			{
				PERFINFO_AUTO_STOP();
				continue;
			}

			PERFINFO_AUTO_START("vis", 1);
				good = beaconVisibleFromGroundPos(iPartitionIdx, targetBeacon, beaconFind.sourcePos);
				numRaycasts++;
			PERFINFO_AUTO_STOP();

			if(!good){
				PERFINFO_AUTO_STOP();
				continue;
			}

			*outFoundLOS = 1;
			
			if(	(	!beaconFind.sourceCluster ||
							targetGalaxy->cluster == beaconFind.sourceCluster)
							&&
							(	!beaconFind.sourceBeacon ||
								!beaconFind.sourceGalaxy ||
								beaconGalaxyPathExists(	beaconFind.sourceBeacon,
														targetBeacon,
														pathFindEntity.maxJumpHeight,
														pathFindEntity.canFly)))
			{
				; // still good
			}
			else if(pathFindEntity.useEnterable && 
					targetGalaxy &&
					beaconFind.sourceCluster &&
					targetGalaxy->cluster != beaconFind.sourceCluster)
			{
				// Just make a path to the cluster connection.
				
				Beacon* result = checkForClusterConnection(targetGalaxy);
				
				if(result){
					bestBeacon = result;
					PERFINFO_AUTO_STOP();
					break;
				}
				else
				{
					staticBeaconSortArray.storage[i] = NULL;
					good = false;
				}
			}
			else{
				if(targetGalaxy){
					// Mark galaxy bad for this search instance.

					targetGalaxy->searchInstance = beacon_state.galaxySearchInstance;
					targetGalaxy->isGoodForSearch = 0;
					good = false;
				}
			}

			if(good)
			{
				F32 yDiff = vecY(targetBeacon->pos) - vecY(startPos);

				beaconDist += MAX(0, yDiff);

				if(sourceTargetDist)
					beaconDist += distance3XZ(targetBeacon->pos, targetPos) - sourceTargetDist;

				if(hasRaisedOnly)
				{
					bestRaisedConnBeacon = targetBeacon;
					PERFINFO_AUTO_STOP();
					continue;
				}
				else if(beaconDist < closestBeaconDist)
				{
					closestBeaconDist = beaconDist;
					bestBeacon = targetBeacon;
				}
			}
		PERFINFO_AUTO_STOP();
	}
	if(numRaycasts>=10)
		objLog(LOG_PATHPERF, GLOBALTYPE_NONE, 0, 0, NULL, (Vec3*)&startPos, NULL, "GBBG_Fail_Raycast", NULL, "");
	
	return bestBeacon ? bestBeacon : bestRaisedConnBeacon;
}

static Beacon* getBestBeaconAir(int iPartitionIdx, int* outFoundLOS){
	int i;
	int numRaycasts = 0;
	Beacon *bestBeacon = NULL;

	for(i = 0; i < staticBeaconSortArray.size && numRaycasts<10; i++){
		Beacon* targetBeacon = staticBeaconSortArray.storage[i];
		BeaconBlock* targetBlock;
		BeaconBlock* targetGalaxy;
		BeaconPartitionData *targetPartition;
		int good;

		if(	!targetBeacon
			||
			!targetBeacon->gbConns.size && !targetBeacon->rbConns.size)
		{
			continue;
		}

		targetPartition = beaconGetPartitionData(targetBeacon, pathFindEntity.partition, false);
		targetBlock = targetPartition->block;
		targetGalaxy = targetBlock ? targetBlock->galaxy : NULL;
		
		// isGoodForSearch is always 0, but searchInstance only gets updated when the
		// galaxy is actually bad, so it works I guess...
		if(targetGalaxy &&
			(targetGalaxy->searchInstance == beacon_state.galaxySearchInstance &&
			!targetGalaxy->isGoodForSearch
			||
			!pathFindEntity.useEnterable && beaconFind.sourceCluster &&
			targetGalaxy->cluster != beaconFind.sourceCluster))
		{
			// This galaxy was already found to be bad, so ignore it.
			staticBeaconSortArray.storage[i] = NULL;
			continue;
		}

		PERFINFO_AUTO_START("air", 1);
			good = true;
			if(!pathFindEntity.useAvoid && 
				pathFindEntity.entref && 
				(aiShouldAvoidBeacon(pathFindEntity.entref, targetBeacon, 0) ||
				 aiShouldAvoidLine(pathFindEntity.entref, NULL, beaconFind.sourcePos, targetBeacon, 0, NULL)))
			{
				staticBeaconSortArray.storage[i] = NULL;
				good = false;
			}

			if(!good)
			{
				PERFINFO_AUTO_STOP();
				continue;
			}

			PERFINFO_AUTO_START("vis", 1);
				good = beaconVisibleFromAirPos(iPartitionIdx, targetBeacon, beaconFind.sourcePos);
				numRaycasts++;
			PERFINFO_AUTO_STOP();

			if(!good){
				PERFINFO_AUTO_STOP();
				continue;
			}

			*outFoundLOS = 1;
			
			if(	(	!targetGalaxy ||
							!beaconFind.sourceCluster ||
							targetGalaxy->cluster == beaconFind.sourceCluster)
							&&
							(	!beaconFind.sourceBeacon ||
								!beaconFind.sourceGalaxy ||
								beaconGalaxyPathExists(	beaconFind.sourceBeacon,
														targetBeacon,
														pathFindEntity.maxJumpHeight,
														pathFindEntity.canFly)))
			{
				bestBeacon = targetBeacon;
				PERFINFO_AUTO_STOP();
				break;
			}
			else if(pathFindEntity.useEnterable &&
					targetGalaxy && 
					beaconFind.sourceCluster &&
					targetGalaxy->cluster != beaconFind.sourceCluster)
			{
				// Just make a path to the cluster connection.
				
				Beacon* result = checkForClusterConnection(targetGalaxy);
				
				if(result){
					bestBeacon = result;
					PERFINFO_AUTO_STOP();
					break;
				}else{
					staticBeaconSortArray.storage[i] = NULL;
				}
			}
			else{
				if(targetGalaxy){
					// Mark galaxy bad for this search instance.
				
					targetGalaxy->searchInstance = beacon_state.galaxySearchInstance;
					targetGalaxy->isGoodForSearch = 0;
				}
			}
		PERFINFO_AUTO_STOP();
	}

	if(numRaycasts>=10)
		objLog(LOG_PATHPERF, GLOBALTYPE_NONE, 0, 0, NULL, &beaconFind.sourcePos, NULL, "GBBA_Fail_Raycast", NULL, "");
	
	return bestBeacon;
}

static Beacon* getBestBeaconNoLOS(){
	int i;
	
	for(i = 0; i < staticBeaconSortArray.size; i++){
		Beacon* beacon = staticBeaconSortArray.storage[i];
		
		if(	beacon &&
			(beacon->gbConns.size || beacon->rbConns.size))
		{
			BeaconPartitionData *partition = beaconGetPartitionData(beacon, pathFindEntity.partition, false);
			BeaconBlock* block = partition->block;
			BeaconBlock* galaxy = block ? block->galaxy : NULL;

			if(	pathFindEntity.useEnterable && 
				galaxy &&
				beaconFind.sourceCluster &&
				galaxy->cluster != beaconFind.sourceCluster)
			{
				// Just make a path to the cluster connection.
				
				Beacon* result = checkForClusterConnection(galaxy);

				if(result){
					return result;
				}else{
					staticBeaconSortArray.storage[i] = NULL;
				}
			}
			else if(galaxy &&
					(	galaxy->searchInstance == beacon_state.galaxySearchInstance &&
						!galaxy->isGoodForSearch
						||
						beaconFind.sourceCluster &&
						galaxy->cluster != beaconFind.sourceCluster))
			{
				// This galaxy or cluster was already found to be bad, so ignore it.
				continue;
			}
			else if(beaconGalaxyPathExists(beaconFind.sourceBeacon, beacon, pathFindEntity.maxJumpHeight, pathFindEntity.canFly)){
				return beacon;
			}
			else{
				if(galaxy){
					// Mark galaxy bad for this search instance.

					galaxy->searchInstance = beacon_state.galaxySearchInstance;
					galaxy->isGoodForSearch = 0;
				}
				
				//printf(	"rejecting (%1.1f, %1.1f, %1.1f) to (%1.1f, %1.1f, %1.1f)\n",
				//		posParamsXYZ(sourceBeacon),
				//		posParamsXYZ(beacon));
			}
		}
	}
	
	return NULL;
}

static StashTable htBadDestinations;

void beaconClearBadDestinationsTable()
{
	if( htBadDestinations )
		stashTableClear(htBadDestinations);
}

// Notice that GCCB_IGNORE_LOS only works if a source beacon is passed in.

Beacon* beaconGetClosestCombatBeacon(	int iPartitionIdx,
										const Vec3 sourcePos,
										const Vec3 targetPos,
										F32 maxLOSRayRadius,
										Beacon* sourceBeacon,
										GCCBflags flags,
										S32* losFailed)
{
	bool ranMakeArray100 = false;
	bool ranMakeArray300 = false;
	char hashKey[500];
	StashElement badElement = NULL;
	Beacon* bestBeacon = NULL;
	int foundLOS = 0;
	S64 start, end;
	BeaconPartitionData *bestPartition;

	GET_CPU_TICKS_64(start);

	hashKey[0] = 0;

	if(losFailed){
		*losFailed = 0;
	}

	if(combatBeaconArray.size==0)
		return NULL;

	beaconCheckBlocksNeedRebuild(pathFindEntity.partition);

	//Fill out beaconFind struct
	beaconFind.sourceCluster = NULL;
	beaconFind.sourceGalaxy = NULL;
	copyVec3(sourcePos, beaconFind.sourcePos);
	beaconFind.sourceBeacon = sourceBeacon;
	beaconFind.wantClusterConnect = 0;
	beaconFind.clusterConnTargetBeacon = NULL;
	beaconFind.inAvoid = (flags & GCCB_STARTS_IN_AVOID) != 0;

	if(sourceBeacon){
		BeaconPartitionData *sourcePartition = beaconGetPartitionData(sourceBeacon, pathFindEntity.partition, false);
		beaconFind.sourceGalaxy = sourcePartition->block->galaxy;
		
		if(beaconFind.sourceGalaxy){
			beaconFind.sourceCluster = beaconFind.sourceGalaxy->cluster;
		}
	}
	
	PERFINFO_AUTO_START("beaconMakeSortedNearbyBeaconArray", 1);
	
		if(!staticBeaconSortArray.maxSize){
			beaconPathInit(1);
		}

		// Get a list of beacons sorted by distance from sourcePos.
		beaconMakeSortedNearbyBeaconArray(iPartitionIdx, sourcePos, 30.0);

		if(staticBeaconSortArray.size < 3 || ((Beacon*)staticBeaconSortArray.storage[staticBeaconSortArray.size - 1])->userFloat > SQR(30))
		{
			ranMakeArray100 = true;
			PERFINFO_AUTO_START("beaconMakeSortedNearbyBeaconArray100", 1);
			beaconMakeSortedNearbyBeaconArray(iPartitionIdx, sourcePos, 100.0);
			PERFINFO_AUTO_STOP();

			if(staticBeaconSortArray.size < 3)
			{
				ranMakeArray300 = true;

				PERFINFO_AUTO_START("beaconMakeSortedNearbyBeaconArray300", 1);
				beaconMakeSortedNearbyBeaconArray(iPartitionIdx, sourcePos, 300.0);
				PERFINFO_AUTO_STOP();
			}
		}

	PERFINFO_AUTO_STOP();
		
	beacon_state.galaxySearchInstance++;

	if(!(flags & GCCB_IGNORE_LOS)){
		
		sprintf(hashKey, "%1.3f,%1.3f,%1.3f,%d", vecParamsXYZ(sourcePos), flags );

		if(!htBadDestinations || !stashFindElement(htBadDestinations, hashKey, &badElement))
		{
			PERFINFO_AUTO_START("beaconFind - LOS", 1);
			
				// Get the closest beacon that matches the condition.

				if(!pathFindEntity.canFly){
					bestBeacon = getBestBeaconGround(iPartitionIdx, sourcePos, targetPos, &foundLOS);

					if(!bestBeacon && !ranMakeArray100)
					{
						ranMakeArray100 = true;
						PERFINFO_AUTO_START("beaconMakeSortedNearbyBeaconArray100", 1);
						beaconMakeSortedNearbyBeaconArray(iPartitionIdx, sourcePos, 100.0);
						PERFINFO_AUTO_STOP();

						if(staticBeaconSortArray.size < 3 && !ranMakeArray300)
						{
							ranMakeArray300 = true;

							PERFINFO_AUTO_START("beaconMakeSortedNearbyBeaconArray300", 1);
							beaconMakeSortedNearbyBeaconArray(iPartitionIdx, sourcePos, 300.0);
							PERFINFO_AUTO_STOP();
						}

						bestBeacon = getBestBeaconGround(iPartitionIdx, sourcePos, targetPos, &foundLOS);
					}
				}
							

				if(!bestBeacon){
					bestBeacon = getBestBeaconAir(iPartitionIdx, &foundLOS);

					if(!bestBeacon && !ranMakeArray100)
					{
						ranMakeArray100 = true;
						PERFINFO_AUTO_START("beaconMakeSortedNearbyBeaconArray100", 1);
						beaconMakeSortedNearbyBeaconArray(iPartitionIdx, sourcePos, 100.0);
						PERFINFO_AUTO_STOP();

						if(staticBeaconSortArray.size < 3 && !ranMakeArray300)
						{
							ranMakeArray300 = true;

							PERFINFO_AUTO_START("beaconMakeSortedNearbyBeaconArray300", 1);
							beaconMakeSortedNearbyBeaconArray(iPartitionIdx, sourcePos, 300.0);
							PERFINFO_AUTO_STOP();
						}

						bestBeacon = getBestBeaconAir(iPartitionIdx, &foundLOS);
					}
				}
				
				if(losFailed){
					*losFailed = !foundLOS;
				}
			PERFINFO_AUTO_STOP();
		}
	}
	
	if( !bestBeacon &&
		sourceBeacon &&
		beaconFind.sourceGalaxy)
	{
		if(	!badElement &&
			!(flags & GCCB_IGNORE_LOS) &&
			!foundLOS)
		{
			assert(hashKey[0]);
			
			if(!htBadDestinations){
				htBadDestinations = stashTableCreateWithStringKeys(500, StashDeepCopyKeys_NeverRelease);
			}
			
			if(stashGetCount(htBadDestinations) > 1000){
				stashTableClear(htBadDestinations);
			}
			
			stashAddPointer(htBadDestinations, hashKey, "nothing", false);
		}
		
		// No beacon found yet, so find the closest good beacon. 
		
		if( !(flags & GCCB_REQUIRE_LOS) &&
			(	!foundLOS ||
				!(flags & GCCB_IF_ANY_LOS_ONLY_LOS)))
		{
			PERFINFO_AUTO_START("beaconFind - NoLOS", 1);
			bestBeacon = getBestBeaconNoLOS();
			PERFINFO_AUTO_STOP();

			if(	!bestBeacon &&
				!ranMakeArray100)
			{
				ranMakeArray100 = true;
				PERFINFO_AUTO_START("beaconMakeSortedNearbyBeaconArray100", 1);
				beaconMakeSortedNearbyBeaconArray(iPartitionIdx, sourcePos, 100.0);
				PERFINFO_AUTO_STOP();

				PERFINFO_AUTO_START("beaconFind - NoLOS - after second mSNBA", 1);
				bestBeacon = getBestBeaconNoLOS();
				PERFINFO_AUTO_STOP();
			}
		}
	}

	GET_CPU_TICKS_64(end);
	
	if(!bestBeacon){
		return NULL;
	}

	bestPartition = beaconGetPartitionData(bestBeacon, pathFindEntity.partition, false);
	if(!bestPartition->block)
		return NULL;
	
	return bestBeacon;
}

int beaconGalaxyPathExists(Beacon* source, Beacon* target, F32 maxJumpHeight, S32 canFly)
{
	AStarSearchData *searchData = beacon_state.astarDataOther;
	static struct {
		char*				name;
		PERFINFO_TYPE*		timer;
	} galaxySearchTimer[MAX_BEACON_GALAXY_GROUP_COUNT];
	
	NavSearchFunctions galaxySearchFunctions = {
		(NavSearchCostToTargetFunction)		beaconGalaxySearchCostToTarget,
		(NavSearchCostFunction)				beaconGalaxySearchCost,
		(NavSearchGetConnectionsFunction)	beaconGalaxySearchGetConnections,
		(NavSearchOutputPath)				NULL,
		(NavSearchCloseNode)				NULL,
		(NavSearchGetNodeInfo)				NULL,
		(NavSearchReportClosedInfo)			NULL,
	};

	BeaconBlock* sourceGalaxy;
	BeaconBlock* targetGalaxy;
	F32 heightRemaining;
	int galaxySet = 0;
	BeaconStatePartition *partition = beaconStatePartitionGet(pathFindEntity.partition, false);
	BeaconPartitionData *sourcePartition = beaconGetPartitionData(source, pathFindEntity.partition, false);
	BeaconPartitionData *targetPartition = beaconGetPartitionData(target, pathFindEntity.partition, false);
	
	beaconCheckBlocksNeedRebuild(pathFindEntity.partition);

	if(	!sourcePartition->block ||
		!targetPartition->block)
	{
		return 0;
	}
	
	sourceGalaxy = sourcePartition->block->galaxy;
	targetGalaxy = targetPartition->block->galaxy;
	
	if(!sourceGalaxy || !targetGalaxy || sourceGalaxy->cluster != targetGalaxy->cluster){
		return 0;
	}
	else if(canFly){
		return 1;
	}

	if (maxJumpHeight > beacon_galaxy_group_count * beaconGalaxyGroupJumpIncrement)
	{
		volatile int r = 1+1;
	}

	for(heightRemaining = maxJumpHeight;
		heightRemaining >= beaconGalaxyGroupJumpIncrement;
		heightRemaining -= beaconGalaxyGroupJumpIncrement)
	{
		if(	!sourceGalaxy->galaxy ||
			!targetGalaxy->galaxy)
		{
			break;
		}

		sourceGalaxy = sourceGalaxy->galaxy;
		targetGalaxy = targetGalaxy->galaxy;
		galaxySet++;

		if(sourceGalaxy == targetGalaxy){
			return 1;
		}
	}

	if(sourceGalaxy == targetGalaxy){
		return 1;
	}

	pathFindEntity.maxJumpHeight = maxJumpHeight;

	searchData->sourceNode = sourceGalaxy;
	searchData->targetNode = targetGalaxy;
	searchData->searching = 0;
	searchData->pathWasOutput = 0;
	searchData->steps = 0;
	searchData->nodeAStarInfoOffset = offsetof(BeaconBlock, astarInfo);

	blockSearch.targetBlock = targetGalaxy;

	PERFINFO_RUN(
		if(!galaxySearchTimer[0].name)
		{
			int i;
			for(i = 0; i < beacon_galaxy_group_count; i++)
			{
				char buffer[100];
				sprintf(buffer, "A* galaxy (%1.1fft)", (F32)(i * beaconGalaxyGroupJumpIncrement));
				galaxySearchTimer[i].name = strdup(buffer);
			}
		}
	);

	PERFINFO_AUTO_START_STATIC(galaxySearchTimer[galaxySet].name, &galaxySearchTimer[galaxySet].timer, 1);
		AStarSearch(searchData, &galaxySearchFunctions);
	PERFINFO_AUTO_STOP();
	
	return searchData->pathWasOutput;
}

int beaconGetNextClusterWaypoint(	Beacon* sourceBeacon,
									Beacon* targetBeacon,
									Vec3 outPos)
{
	AStarSearchData *searchData = beacon_state.astarDataOther;
	BeaconBlock* sourceCluster, *targetCluster;
	BeaconPartitionData *sourcePartition = beaconGetPartitionData(sourceBeacon, pathFindEntity.partition, false);
	BeaconPartitionData *targetPartition = beaconGetPartitionData(targetBeacon, pathFindEntity.partition, false);

	sourceCluster = SAFE_MEMBER3(sourcePartition, block, galaxy, cluster);
	targetCluster = SAFE_MEMBER3(targetPartition, block, galaxy, cluster);

	if(	!sourceCluster ||
		!targetCluster)
	{
		return 0;
	}
	
	if(sourceCluster == targetCluster){
		return 0;  //Return 0 because it means the outPos is invalid, not a path was found
	}
	
	searchData->sourceNode = sourceCluster;
	searchData->targetNode = targetCluster;
	searchData->searching = 0;
	searchData->pathWasOutput = 0;
	searchData->steps = 0;
	searchData->nodeAStarInfoOffset = offsetof(BeaconBlock, astarInfo);

	clusterSearch.connToNextCluster = NULL;

	beaconFind.sourceBeacon = sourceBeacon;
	blockSearch.sourceBlock = sourceCluster;
	blockSearch.targetBlock = targetCluster;

	PERFINFO_AUTO_START("A* cluster", 1);
		AStarSearch(searchData, &beacon_state.searchFuncsCluster);
	PERFINFO_AUTO_STOP();
	
	if(clusterSearch.connToNextCluster)
	{
		copyVec3(clusterSearch.connToNextCluster->source.pos, outPos);
		
		return 1;
	}
	
	return 0;
}

void beaconPathFindSimpleMakeWaypoint(int iPartitionIdx, NavPath* path, Beacon* bcn, BeaconConnection *conn, BeaconScoreFunction funcCheckBeacon, void *userdata, int addToHead)
{
	NavPathWaypoint *wp = createNavPathWaypoint();

	wp->beacon = bcn;
	wp->connectionToMe = conn;
	wp->dontShortcut = 1;

	if(!pathFindEntity.alwaysFly)
	{
		wp->connectType = NAVPATH_CONNECT_GROUND;
		copyVec3(bcn->pos, wp->pos);
		vecY(wp->pos) -= beaconGetFloorDistance(iPartitionIdx, bcn);
		vecY(wp->pos) += 2;
	}
	else
	{
		int i;
		wp->connectType = NAVPATH_CONNECT_FLY;
		copyVec3(bcn->pos, wp->pos);
		// Find a valid starting level if one isn't specified
		if(!conn)
		{
			for(i=0; i<bcn->rbConns.size; i++)
			{
				conn = bcn->rbConns.storage[i];

				if(funcCheckBeacon(bcn, conn, userdata, NULL, NULL))
				{
					// Good!
					break;
				}
			}
		}
		if(conn)
		{
			F32 minH = 1, maxH = 1;
			funcCheckBeacon(bcn, conn, userdata, &minH, &maxH);
			wp->pos[1] += randomPositiveF32()*(maxH-minH)+minH;
		}
	}

	if(addToHead)
	{
		navPathAddHead(path, wp);
	}
	else
	{
		navPathAddTail(path, wp);
	}
}

static int beaconPathFindSimpleHelper(int iPartitionIdx, NavPath* path, Beacon* srcIn, S32 maxPath,  
									  BeaconScoreFunction funcBeaconScore, void *userdata)
{
	Beacon* curr = srcIn;
	if (maxPath <= 0)
		maxPath = 30;

	do 
	{
		int i;
		int bestScore;
		BeaconConnection* conn;
		BeaconConnection* bestConn;
		Beacon* dst;
		Beacon* best = NULL;
		Array *conns = NULL;
		BeaconPartitionData *bpd = beaconGetPartitionData(curr, pathFindEntity.partition, false);

		curr->searchInstance = beacon_state.beaconSearchInstance;
	
		if(!bpd->block->madeConnections){
			return 0;
		}

		conns = pathFindEntity.alwaysFly ? &curr->rbConns : &curr->gbConns;

		if(!conns->size){
			return 1;
		}

		for(i = 0; i < conns->size; i++){
			int score;

			conn = conns->storage[i];
			dst = conn->destBeacon;

			if(dst->searchInstance == beacon_state.beaconSearchInstance){
				continue;
			}

			score = funcBeaconScore(curr, conn, userdata, NULL, NULL);

			if(score < 0){
				if(score == BEACONSCORE_FINISHED){
					// Success, this is a good destination.

					beaconPathFindSimpleMakeWaypoint(iPartitionIdx, path, dst, conn, funcBeaconScore, userdata, 0);
					return 1;
				}
				else if(score == BEACONSCORE_CANCEL){
					// Kill the search.

					return 0;
				}
			}
			else if(score && (!best || score > bestScore)){
				best = dst;
				bestConn = conn;
				bestScore = score;
			}
		}

		if(best)
		{
			beaconPathFindSimpleMakeWaypoint(iPartitionIdx, path, best, bestConn, funcBeaconScore, userdata, 0);
		}

		curr = best;
	} while(curr && --maxPath > 0);

	return 1;
}

void navPathClearEx(NavPath *path, const char* file, int line)
{
	eaDestroyExFileLineEx(&path->waypoints, destroyNavPathWaypointEx, file, line);
	path->circular = 0;
	path->pingpong = 0;
	path->pingpongRev = 0;
	path->curWaypoint = 0;
}

void navPathCopy(NavPath *src, NavPath *dst)
{
	eaClearExFileLine(&dst->waypoints, destroyNavPathWaypointEx);

	eaCopy(&dst->waypoints, &src->waypoints);
	dst->curWaypoint = src->curWaypoint;
	dst->circular = src->circular;
	dst->pingpong = src->pingpong;
	dst->pingpongRev = src->pingpongRev;

	eaClear(&src->waypoints);
	navPathClear(src);
}

void beaconPathFindSimple(int iPartitionIdx, NavPath* path, Beacon* src, S32 maxPath, BeaconScoreFunction funcCheckBeacon, void *userdata)
{
	NavPathWaypoint* wp = NULL;

	navPathClear(path);

	if(!src)
		return;
	
	beaconCheckBlocksNeedRebuild(pathFindEntity.partition);

	beacon_state.beaconSearchInstance++;

	if(!pathFindEntity.alwaysFly && !src->gbConns.size)
	{
		// Get in a good state for ground starts
		int i, n;
		static BeaconConnection** possibleConns = NULL;

		if(possibleConns)
			eaSetSize(&possibleConns, 0);

		for(i = 0; i < src->rbConns.size; i++)
		{
			BeaconConnection* conn = src->rbConns.storage[i];

			if(conn->destBeacon->gbConns.size)
				eaPush(&possibleConns, conn);
		}

		n = eaSize(&possibleConns);

		if(n)
		{
			BeaconConnection* conn = possibleConns[randomU32() % n];

			wp = createNavPathWaypoint();

			wp->beacon = src;
			wp->dontShortcut = true;
			wp->connectType = NAVPATH_CONNECT_GROUND;
			copyVec3(src->pos, wp->pos);
			vecY(wp->pos) -= beaconGetFloorDistance(iPartitionIdx, src);
			vecY(wp->pos) += 2;

			navPathAddHead(path, wp);

			path->curWaypoint = 0;

			wp = createNavPathWaypoint();

			wp->beacon = conn->destBeacon;
			wp->connectType = NAVPATH_CONNECT_JUMP;
			copyVec3(conn->destBeacon->pos, wp->pos);
			vecY(wp->pos) -= beaconGetFloorDistance(iPartitionIdx, conn->destBeacon);
			vecY(wp->pos) += 2;
			wp->connectionToMe = conn;

			navPathAddTail(path, wp);

			return;
		}
	}

	path->curWaypoint = 0;

	beaconPathFindSimpleMakeWaypoint(iPartitionIdx, path, src, NULL, funcCheckBeacon, userdata, 1);

	beaconPathFindSimpleHelper(iPartitionIdx, path, src, maxPath, funcCheckBeacon, userdata);
}

void beaconPathFindSimpleStart(int iPartitionIdx,
							   NavPath* path, 
							   const Vec3 pos, 
							   S32 maxPath, 
							   BeaconScoreFunction funcCheckBeacon, 
							   void *userdata)
{
	Beacon *beacon = NULL;
	navPathClear(path);

	beacon = beaconGetClosestCombatBeacon(iPartitionIdx, pos, NULL, 1, NULL, GCCB_PREFER_LOS, NULL);
	
	if(beacon && funcCheckBeacon(beacon, NULL, userdata, NULL, NULL)){
		beaconPathFindSimple(iPartitionIdx, path, beacon, maxPath, funcCheckBeacon, userdata);
	}
}

static Entity* getFirstDoorEntity(BeaconDynamicConnection* dynConn){
	Entity* bestDoor = NULL;
	
	return bestDoor;
}

void beaconPathInit(int forceUpdate){
	int dx = 3;
	int dz = 3;
	const int dxz = dx * dz;
	int i;
	int maxBeaconsInBigCube = 0;
	int min_y = INT_MAX;
	int max_y = INT_MIN;
	BeaconStatePartition *partition = beaconStatePartitionGet(0, true);
	
	if(staticBeaconSortArray.maxSize){
		if(forceUpdate){
			destroyArrayPartial(&staticBeaconSortArray);
		}else{
			return;
		}
	}

	if(!partition->combatBeaconGridBlockArray.size){
		return;
	}
	
	PERFINFO_AUTO_START("beaconPathInit", 1);
	
		for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++){
			BeaconBlock* block = partition->combatBeaconGridBlockArray.storage[i];
			
			if(block->pos[1] < min_y){
				min_y = block->pos[1];
			}

			if(block->pos[1] > max_y){
				max_y = block->pos[1];
			}
		}
		
		for(i = 0; i < partition->combatBeaconGridBlockArray.size; i++){
			BeaconBlock* block = partition->combatBeaconGridBlockArray.storage[i];
			int j;
			int total = 0;
			
			for(j = 0; j < dxz; j++){
				BeaconBlock* block2;
				int y;
				
				for(y = min_y; y <= max_y; y++){
					int x = block->pos[0] - (dx/2) + j % dz;
					int z = block->pos[2] - (dz/2) + j / dz;
					
					block2 = beaconGetGridBlockByCoords(partition, x, y, z, 0);
																
					if(block2){
						total += block2->beaconArray.size;
					}
				}
			}
			
			if(total > maxBeaconsInBigCube){
				maxBeaconsInBigCube = total;
			}
		}
		
		staticBeaconSortMinY = min_y;
		staticBeaconSortMaxY = max_y;
		
		if(maxBeaconsInBigCube){
			initArray(&staticBeaconSortArray, maxBeaconsInBigCube);
		}
	PERFINFO_AUTO_STOP();
}

void beaconPathSetDoorCallback(BeaconPathDoorCallback cb)
{
	pathFindCallbacks.doorcb = cb;
}

bool beaconFindNextConnection(int iPartitionIdx, EntityRef ref, Vec3 src, Vec3 dest, Vec3 result, const char* triviaStr)
{
	Beacon *bcnsrc;
	Beacon *bcndst;
	S64 tStart, tEnd;
	S64 tSrc = 0, tDst = 0, tPath = 0;
	bool good = true;

	copyVec3(dest, result);

	beaconSetPathFindEntity(ref, 0, 6);
	{
		GET_CPU_TICKS_64(tStart);
		bcnsrc = beaconGetClosestCombatBeacon(iPartitionIdx, src, NULL, 0, NULL, GCCB_PREFER_LOS, NULL);
		// It is common for artists to place things slightly embedded
		// in terrain / geo.  Moving up on Y compensates for this.
		if(!bcnsrc)
		{
			Vec3 srcMovedUp;
			copyVec3(src, srcMovedUp);
			vecY(srcMovedUp) += 1;
			bcnsrc = beaconGetClosestCombatBeacon(iPartitionIdx, srcMovedUp, NULL, 0, NULL, GCCB_PREFER_LOS, NULL);
		}
		GET_CPU_TICKS_64(tEnd);

		tSrc = tEnd - tStart;

		good = !!bcnsrc;
	}

	if(good)
	{
		GET_CPU_TICKS_64(tStart);
		bcndst = beaconGetClosestCombatBeacon(iPartitionIdx, dest, NULL, 0, NULL, GCCB_PREFER_LOS, NULL);
		// It is common for artists to place things slightly embedded
		// in terrain / geo.  Moving up on Y compensates for this.
		if(!bcndst)
		{
			Vec3 destMovedUp;
			copyVec3(dest, destMovedUp);
			vecY(destMovedUp) += 1;
			bcndst = beaconGetClosestCombatBeacon(iPartitionIdx, destMovedUp, NULL, 0, NULL, GCCB_PREFER_LOS, NULL);
		}
		GET_CPU_TICKS_64(tEnd);

		tDst = tEnd - tStart;

		good = !!bcndst;
	}

	if(good)
	{
		GET_CPU_TICKS_64(tStart);
		good = beaconGetNextClusterWaypoint(bcnsrc, bcndst, result);
		GET_CPU_TICKS_64(tEnd);

		tPath = tEnd-tStart;
	}

	{
		BeaconPathLogEntry *entry = bcnPathLogAddEntry(tSrc, tDst, tPath);

		if(entry)
		{
			entry->abs_time = ABS_TIME;
			entry->ownerFile = __FILE__;
			entry->ownerLine = __LINE__;
			copyVec3(src, entry->sourcePos);
			copyVec3(dest, entry->targetPos);

			entry->triviaStr = StructAllocString(triviaStr);
		}
	}

	return good;
}

StashTable bcnBadConnectionStash = NULL;

U32 beaconConnectionGetTimeBad(BeaconConnection* conn)
{
	U32 timeBad = 0;
	if(!conn->wasBadEver)
		return 0;

	if(!bcnBadConnectionStash)
		return 0;

	stashAddressFindInt(bcnBadConnectionStash, conn, &timeBad);

	return timeBad;
}

void beaconConnectionSetBad(BeaconConnection* conn)
{
	U32 time = ABS_TIME;
	conn->wasBadEver = true;

	if(!bcnBadConnectionStash)
		bcnBadConnectionStash = stashTableCreateAddress(20);

	stashAddressAddInt(bcnBadConnectionStash, conn, time, true);
}

void beaconConnectionResetBad(BeaconConnection* conn)
{
	if(!conn->wasBadEver)
		return;

	if(!bcnBadConnectionStash)
		return;

	stashAddressRemoveInt(bcnBadConnectionStash, conn, NULL);
}

void beaconConnectionClearBadness(BeaconStatePartition *partition)
{
	if(bcnBadConnectionStash)
		stashTableClear(bcnBadConnectionStash);
}

void beaconQueuedPathfindDestroy(BeaconQueuedPathfind *qpf)
{
	navPathClear(&qpf->path);
	estrDestroy(&qpf->resultMsg);
	free(qpf);
}

void beaconPathFindQueueEx(int partitionIdx, NavPath* path, const Vec3 sourcePos, const Vec3 targetPos, const char* trivia, const char* file, int line)
{
	BeaconQueuedPathfind *qpf = NULL;
	qpf = callocStruct(BeaconQueuedPathfind);

	qpf->pfEnt = pathFindEntity;
	qpf->phase = QPF_Source;
	qpf->partitionIdx = partitionIdx;
	qpf->file = file;
	qpf->line = line;
	copyVec3(sourcePos, qpf->sourcePos);
	copyVec3(targetPos, qpf->targetPos);
	qpf->trivia = trivia;

	// I'm tired of all the false positives - AM - increased to 1 ft for safety
	qpf->sourcePos[1] += 1;
	qpf->targetPos[1] += 1;

	eaPush(&beacon_state.queuedPathfinds, qpf);
}

void beaconPathFindBeaconQueue(int partitionIdx, NavPath *path, Beacon *source, Beacon *target)
{
	BeaconQueuedPathfind *qpf = NULL;
	qpf = callocStruct(BeaconQueuedPathfind);

	qpf->pfEnt = pathFindEntity;
	qpf->phase = QPF_Block;
	qpf->partitionIdx = partitionIdx;
	qpf->file = __FILE__;
	qpf->line = __LINE__;
	qpf->sourceBeacon = source;
	copyVec3(source->pos, qpf->sourcePos);
	qpf->targetBeacon = target;
	copyVec3(target->pos, qpf->targetPos);
	qpf->trivia = NULL;

	eaPush(&beacon_state.queuedPathfinds, qpf);
}

static void beaconPathFindFillStaticData(BeaconQueuedPathfind *qpf)
{
	BeaconBlock *srcBlock = NULL, *srcGalaxy = NULL, *srcCluster = NULL;
	BeaconBlock *tgtBlock = NULL;

	// BeaconFind
	ZeroStruct(&beaconFind);

	copyVec3(qpf->sourcePos, beaconFind.searchSrcPos);
	copyVec3(qpf->targetPos, beaconFind.searchDstPos);

	beaconFind.sourceBeacon = qpf->sourceBeacon;
	beaconFind.wantClusterConnect = qpf->wantClusterConnect;
	beaconFind.clusterConnTargetBeacon = qpf->clusterConnTargetBeacon;

	if(beaconFind.sourceBeacon)
	{
		beaconGetBlockData(	beaconFind.sourceBeacon, 
							qpf->partitionIdx, 
							&srcBlock, 
							&beaconFind.sourceGalaxy, 
							&beaconFind.sourceCluster);
	}


	if(qpf->targetBeacon) 
		beaconGetBlockData(qpf->targetBeacon, qpf->partitionIdx, &tgtBlock, NULL, NULL);
	
	// BlockSearch
	blockSearch.sourceBlock = srcBlock;
	blockSearch.targetBlock = tgtBlock;

	// pathFindEntity
	pathFindEntity = qpf->pfEnt;
}

static void beaconPathFindPhaseSource(BeaconQueuedPathfind *qpf)
{
	S64 start_time;
	S64 end_time;
	S64 timeSource;

	// Get the closest beacon to the source.
	PERFINFO_AUTO_START("findSourceBeacon", 1);
	GET_CPU_TICKS_64(start_time);
	
	qpf->sourceBeacon = beaconGetClosestCombatBeacon(qpf->partitionIdx, beaconFind.searchSrcPos, qpf->targetPos, 1, NULL, GCCB_REQUIRE_LOS, NULL);
	
	GET_CPU_TICKS_64(end_time);
	timeSource = end_time-start_time;
	PERFINFO_AUTO_STOP();

	if(!qpf->sourceBeacon)
	{
		qpf->result = NAV_RESULT_NO_SOURCE_BEACON;

		if(qpf->resultMsg)
		{
			estrPrintf(	&qpf->resultMsg,
						"No beacon found near source (%1.1f,%1.1f,%1.1f).",
						vecParamsXYZ(beaconFind.searchSrcPos));
		}

		return;
	}
}

static void beaconPathFindPhaseTarget(BeaconQueuedPathfind *qpf)
{
	S64 start_time;
	S64 end_time;
	S64 timeTarget;

	PERFINFO_AUTO_START("findTargetBeacon", 1);
	{
		S32 losFailed;

		GET_CPU_TICKS_64(start_time);
		qpf->targetBeacon = beaconGetClosestCombatBeacon(	qpf->partitionIdx, 
															beaconFind.searchDstPos, 
															qpf->sourcePos, 
															1, 
															qpf->sourceBeacon, 
															GCCB_REQUIRE_LOS, 
															&losFailed);
		GET_CPU_TICKS_64(end_time);
		timeTarget = end_time-start_time;

		if(losFailed){
			qpf->losFailed = 1;
		}
	}
	PERFINFO_AUTO_STOP();

	if(!qpf->targetBeacon)
	{
		qpf->result = NAV_RESULT_NO_TARGET_BEACON;

		if(qpf->resultMsg){
			estrPrintf(	&qpf->resultMsg,
						"No beacon found near target (%1.1f,%1.1f,%1.1f) from src (%1.1f,%1.1f,%1.1f) and bcn (%1.1f,%1.1f,%1.1f).",
						vecParamsXYZ(beaconFind.searchDstPos),
						vecParamsXYZ(beaconFind.searchSrcPos),
						vecParamsXYZ(qpf->sourceBeacon->pos));
		}

		return;
	}

	// This case is designed to find out if we actually failed to find a reasonable target beacon.  Currently the above code
	// does not actually check for walkability between the target pos and the target beacon it returns.  Adding that may be the
	// correct solution.  Obviously, the 15 foot check is weak. [RMARR - 5/10/13]
	if (distance3Squared(beaconFind.searchDstPos,qpf->targetBeacon->pos) > 15.0f*15.f)
	{
		qpf->result = NAV_RESULT_TARGET_UNREACHABLE;

		if(qpf->resultMsg){
			estrPrintf(	&qpf->resultMsg,
						"No reachable beacon found near target (%1.1f,%1.1f,%1.1f) from src (%1.1f,%1.1f,%1.1f) and bcn (%1.1f,%1.1f,%1.1f).",
						vecParamsXYZ(beaconFind.searchDstPos),
						vecParamsXYZ(beaconFind.searchSrcPos),
						vecParamsXYZ(qpf->sourceBeacon->pos));
		}

		return;
	}

	qpf->wantClusterConnect = beaconFind.wantClusterConnect;
	qpf->clusterConnTargetBeacon = beaconFind.clusterConnTargetBeacon;

	// If we need to take a cluster connection, we have to get there
	if(qpf->wantClusterConnect)
	{
		qpf->actualJumpHeight = qpf->pfEnt.maxJumpHeight;
		qpf->actualJumpHeightCostMult = qpf->pfEnt.jumpHeightCostMult;
		MAX1(qpf->pfEnt.maxJumpHeight, 30);
		MAX1(qpf->pfEnt.jumpHeightCostMult, 4);
	}
}

static NavSearchResultType beaconPathFindBlockPath(BeaconQueuedPathfind *qpf, PathFindEntityData *pfEnt, Beacon* srcBeacon, Beacon *dstBeacon, Vec3 srcPos, Vec3 dstPos, char **estrMsg)
{
	BeaconBlock *srcBlock = NULL, *dstBlock = NULL;
	AStarSearchData *searchData = beacon_state.astarDataStandard;

	beaconGetBlockData(srcBeacon, qpf->partitionIdx, &srcBlock, NULL, NULL);
	beaconGetBlockData(dstBeacon, qpf->partitionIdx, &dstBlock, NULL, NULL);

	if(!searchData->searching)
	{
		Vec3 pos;
		beaconGetClosestAirPos(qpf->partitionIdx, srcBeacon, srcPos, pos);
		srcBeacon->userFloat = vecY(pos);

		beaconGetClosestAirPos(qpf->partitionIdx, dstBeacon, dstPos, pos);
		dstBeacon->userFloat = vecY(pos);

		qpf->destinationHeight = vecY(pos);

		// Make sure that beacons are in a block.

		if(	srcBlock->isGridBlock ||
			dstBlock->isGridBlock ||
			!srcBlock->madeConnections ||
			!dstBlock->madeConnections)
		{

			if(estrMsg && *estrMsg)
			{
				estrPrintf(	estrMsg,
							"Source or destination beacon is still in grid block, or connections not made.");
			}				
			return NAV_RESULT_BLOCK_ERROR;
		}

		// Do the block-level search.

		searchData->sourceNode = srcBlock;
		searchData->targetNode = dstBlock;
		searchData->searching = 0;
		searchData->pathWasOutput = 0;
		searchData->steps = 0;
		searchData->nodeAStarInfoOffset = offsetof(BeaconBlock, astarInfo);
	}

	//Static data setup
	blockSearch.sourceBlock = srcBlock;
	searchData->sourcePos[0] = blockSearch.sourceBlock->pos[0];
	searchData->sourcePos[1] = blockSearch.sourceBlock->pos[1];
	searchData->sourcePos[2] = blockSearch.sourceBlock->pos[2];

	blockSearch.targetBlock = dstBlock;
	searchData->targetPos[0] = blockSearch.targetBlock->pos[0];
	searchData->targetPos[1] = qpf->destinationHeight;
	searchData->targetPos[2] = blockSearch.targetBlock->pos[2];

	if(pfEnt->canFly)
		beacon_state.searchFuncsBlock.closeNode = (NavSearchCloseNode)beaconBlockSearchCloseNode;
	//-----

	//Run search
	PERFINFO_AUTO_START("Block Search 1", 1);
	AStarSearch(searchData, &beacon_state.searchFuncsBlock);
	PERFINFO_AUTO_STOP();

	//Static data teardown
	beacon_state.searchFuncsBlock.closeNode = NULL;

	if(!searchData->searching && !searchData->pathWasOutput)
	{
		if(estrMsg && *estrMsg)
		{
			estrPrintf(estrMsg,
					"No block path!: ^4%d ^0conns, ^4%d ^0nodes (^4%1.f^0,^4%1.f^0,^4%1.f^0) to (^4%1.f^0,^4%1.f^0,^4%1.f^0).",
					searchData->checkedConnectionCount,
					searchData->exploredNodeCount,
					vecParamsXYZ(srcBeacon->pos),
					vecParamsXYZ(dstBeacon->pos));
		}

		return NAV_RESULT_NO_BLOCK_PATH;
	}

	return NAV_RESULT_CONTINUE;
}

static NavSearchResultType beaconPathFindBeaconPath(BeaconQueuedPathfind *qpf, PathFindEntityData *pfEnt, Beacon* srcBeacon, Beacon* dstBeacon, char** estrMsg)
{
	AStarSearchData *searchData = beacon_state.astarDataStandard;
	NavSearchResultType result;

	if(!searchData->searching)
	{
		searchData->searching = 0;
		searchData->pathWasOutput = 0;
		searchData->exploredNodeCount = 0;
		searchData->findClosestOnFail = 0;

		searchData->sourceNode = srcBeacon;
		searchData->targetNode = dstBeacon;
		searchData->steps = 0;
		searchData->nodeAStarInfoOffset = offsetof(Beacon, astarInfo);
		searchData->userData = qpf;
	}

	vecX(searchData->targetPos) = vecX(dstBeacon->pos);
	vecZ(searchData->targetPos) = vecZ(dstBeacon->pos);

	if(pfEnt->canFly){
		vecY(searchData->targetPos) = dstBeacon->userFloat;
	}else{
		vecY(searchData->targetPos) = vecY(dstBeacon->pos);
	}

	if(pfEnt->canFly)
		beacon_state.searchFuncsBeacon.closeNode = (NavSearchCloseNode)beaconSearchCloseNode;

	// Run the search.

	PERFINFO_AUTO_START("Beacon Search", 1);
	AStarSearch(searchData, &beacon_state.searchFuncsBeacon);
	PERFINFO_AUTO_STOP();

	beacon_state.searchFuncsBeacon.closeNode = NULL;

	// Print a message to resultMsg.

	if(!searchData->searching)
	{
		if(searchData->pathWasOutput)
			result = NAV_RESULT_SUCCESS;
		else
			result = NAV_RESULT_NO_BEACON_PATH;

		if(estrMsg && *estrMsg)
		{
			estrPrintf(estrMsg,
				"%s^4%d^d/^4%d ^dbcns, ^4%d ^dconns (^4%1.f^d,^4%1.f^d,^4%1.f^d)-(^4%1.f^d,^4%1.f^d,^4%1.f^d)",
				searchData->pathWasOutput ? "" : "No beacon path!: ",
				searchData->exploredNodeCount,
				staticAvailableBeaconCount,
				searchData->checkedConnectionCount,
				vecX(srcBeacon->pos), pfEnt->canFly ? srcBeacon->userFloat : vecY(srcBeacon->pos), vecZ(srcBeacon->pos), 
				vecX(dstBeacon->pos), pfEnt->canFly ? dstBeacon->userFloat : vecY(dstBeacon->pos), vecZ(dstBeacon->pos));
		}
	}
	else
		result = NAV_RESULT_CONTINUE;

	return result;
}

static void beaconPathFindPhaseBlock(BeaconQueuedPathfind *qpf)
{
	qpf->result = beaconPathFindBlockPath(qpf, &qpf->pfEnt, qpf->sourceBeacon, qpf->targetBeacon, qpf->sourcePos, qpf->targetPos, NULL);
}

static void beaconPathFindPhaseBeacon(BeaconQueuedPathfind *qpf)
{
	qpf->result = beaconPathFindBeaconPath(qpf, &qpf->pfEnt, qpf->sourceBeacon, qpf->targetBeacon, NULL);
}

static void beaconPathFindPhaseDoorBlock(BeaconQueuedPathfind *qpf)
{
	AStarSearchData *searchData = beacon_state.astarDataStandard;

	if(!searchData->searching)
	{
		NavPathWaypoint* pathtail = eaTail(&qpf->path.waypoints);
		NavPathWaypoint* wp = NULL;

		pathFindEntity.maxJumpHeight = qpf->actualJumpHeight;
		pathFindEntity.jumpHeightCostMult = qpf->actualJumpHeightCostMult;

		if(!pathtail)
		{
			// MS: This would be weird, I think.
			return;
		}

		// Add a door connection.
		wp = createNavPathWaypoint();	

		copyVec3(qpf->clusterConnTargetBeacon->pos, wp->pos);
		wp->beacon = qpf->clusterConnTargetBeacon;
		wp->connectType = NAVPATH_CONNECT_ENTERABLE;
		navPathAddTail(&qpf->path, wp);

		qpf->targetBeacon = beaconGetClosestCombatBeacon(qpf->partitionIdx, qpf->targetPos, NULL, 1, qpf->clusterConnTargetBeacon, GCCB_PREFER_LOS, NULL);

		if(!qpf->targetBeacon)
		{
			qpf->result = NAV_RESULT_NO_TARGET_BEACON;

			return;
		}
	}

	qpf->result = beaconPathFindBlockPath(qpf, &qpf->pfEnt, qpf->clusterConnTargetBeacon, qpf->targetBeacon, qpf->clusterConnTargetBeacon->pos, qpf->targetPos, NULL);
}

static void beaconPathFindPhaseDoorBeacon(BeaconQueuedPathfind *qpf)
{
	qpf->result = beaconPathFindBeaconPath(qpf, &qpf->pfEnt, qpf->clusterConnTargetBeacon, qpf->targetBeacon, NULL);
}

static void beaconPathFindContinue(BeaconQueuedPathfind *qpf, S64 tickLimit)
{
	S64 ticksStart;
	S64 ticksCur;

	beaconCheckBlocksNeedRebuild(qpf->partitionIdx);

	GET_CPU_TICKS_64(ticksStart);
	ticksCur = ticksStart;

	beacon_state.astarDataStandard->userData = qpf;

	while(qpf->result==NAV_RESULT_CONTINUE)
	{
		S64 ticksRemaining = tickLimit - (ticksCur - ticksStart);

		if(tickLimit > 0)
		{
			if(ticksRemaining < 0)
				return;

			beacon_state.astarDataStandard->maxTicks = ticksRemaining;
		}
		else
			beacon_state.astarDataStandard->maxTicks = 0;

		beaconPathFindFillStaticData(qpf);

		switch(qpf->phase)
		{
			xcase QPF_Source: {
				beaconPathFindPhaseSource(qpf);

				qpf->phase = QPF_Target;
			}
			xcase QPF_Target: {
				beaconPathFindPhaseTarget(qpf);

				qpf->phase = QPF_Block;
			}
			xcase QPF_Block: {
				beaconPathFindPhaseBlock(qpf);

				if(!beacon_state.astarDataStandard->searching)
					qpf->phase = QPF_Beacon;
			}
			xcase QPF_Beacon: {
				beaconPathFindPhaseBeacon(qpf);

				if(!beacon_state.astarDataStandard->searching)
				{
					if(	beaconFind.wantClusterConnect &&
						beaconFind.clusterConnTargetBeacon && 
						qpf->result == NAV_RESULT_SUCCESS)
					{
						qpf->result = NAV_RESULT_CONTINUE;
						qpf->phase = QPF_DoorBlock;
					}
				}
			}
			xcase QPF_DoorBlock: {
				beaconPathFindPhaseDoorBlock(qpf);

				if(!beacon_state.astarDataStandard->searching)
					qpf->phase = QPF_DoorBeacon;
			}
			xcase QPF_DoorBeacon: {
				beaconPathFindPhaseDoorBeacon(qpf);

				if(!beacon_state.astarDataStandard->searching)
					assert(qpf->result!=NAV_RESULT_CONTINUE);
			}
		}

		GET_CPU_TICKS_64(ticksCur);
	}
}

NavSearchResultType beaconPathFindSync(BeaconQueuedPathfind *qpf, NavPath *path)
{
	int numTicks = BEACON_PATHFIND_CPU_TICK_LIMIT;
	S64 ticksStart, ticksEnd;
	NavSearchResultType result;

	if(!g_BcnExpensivePathsFail)
		numTicks = -1;

	if(g_BcnForcePathfindTimeout)
		qpf->result = NAV_RESULT_TIMEOUT;

	navPathClear(path);

	GET_CPU_TICKS_64(ticksStart);
	beaconPathFindContinue(qpf, numTicks);
	GET_CPU_TICKS_64(ticksEnd);

	result = qpf->result;

	if(qpf->result==NAV_RESULT_CONTINUE || result==NAV_RESULT_TIMEOUT)
	{
		result = NAV_RESULT_TIMEOUT;
		// Clean up data we won't use again, and also don't muddy the future searches
		AStarSearchCancel(beacon_state.astarDataStandard);		
	}

	if(result==NAV_RESULT_TIMEOUT)
	{
		F32 MHz = floor((ticksEnd - ticksStart)/100000000);
		ErrorDetailsf("%s ("LOC_PRINTF_STR")->("LOC_PRINTF_STR") %"FORM_LL"d",
						qpf->trivia, vecParamsXYZ(qpf->sourcePos), vecParamsXYZ(qpf->targetPos),
						ticksEnd - ticksStart);
		Errorf("Pathfind took longer than %.0f000000 cycles, limit is %d", MHz, numTicks);
	}

	if(qpf->result==NAV_RESULT_SUCCESS ||
		qpf->result==NAV_RESULT_PARTIAL)
	{
		navPathCopy(&qpf->path, path);
	}
	// Otherwise, waypoints cleared below in qpf destroy

	beaconQueuedPathfindDestroy(qpf);
	
	return result;
}

NavSearchResultType beaconPathFindEx(int partitionIdx, SA_PARAM_NN_VALID NavPath* path, const Vec3 sourcePos, const Vec3 targetPos, const char* trivia, const char* srcFile, int line)
{
	BeaconQueuedPathfind *qpf = NULL;

	beaconPathFindQueueEx(partitionIdx, path, sourcePos, targetPos, trivia, srcFile, line);

	qpf = eaPop(&beacon_state.queuedPathfinds);

	return beaconPathFindSync(qpf, path);
}

NavSearchResultType beaconPathFindBeacon(int partitionIdx, NavPath *path, Beacon *source, Beacon *target)
{
	BeaconQueuedPathfind *qpf = NULL;

	beaconPathFindBeaconQueue(partitionIdx, path, source, target);

	qpf = eaPop(&beacon_state.queuedPathfinds);

	return beaconPathFindSync(qpf, path);
}

void beaconCheckPathfindQueue(void)
{
	if(!beacon_state.activePathfind)
		beacon_state.activePathfind = eaRemove(&beacon_state.queuedPathfinds, 0);

	if(beacon_state.activePathfind)
		beaconPathFindContinue(beacon_state.activePathfind, BEACON_PATHFIND_CPU_TICK_LIMIT);
}

void beaconPathStartup(void)
{
	NavSearchFunctions clusterSearchFunctions = {
		(NavSearchCostToTargetFunction)		beaconClusterSearchCostToTarget,
		(NavSearchCostFunction)				beaconClusterSearchCost,
		(NavSearchGetConnectionsFunction)	beaconClusterSearchGetConnections,
		(NavSearchOutputPath)				beaconClusterSearchOutputPath,
		(NavSearchCloseNode)				NULL,
		(NavSearchGetNodeInfo)				NULL,
		(NavSearchReportClosedInfo)			NULL,
	};

	NavSearchFunctions galaxySearchFunctions = {
		(NavSearchCostToTargetFunction)		beaconGalaxySearchCostToTarget,
		(NavSearchCostFunction)				beaconGalaxySearchCost,
		(NavSearchGetConnectionsFunction)	beaconGalaxySearchGetConnections,
		(NavSearchOutputPath)				NULL,
		(NavSearchCloseNode)				NULL,
		(NavSearchGetNodeInfo)				NULL,
		(NavSearchReportClosedInfo)			NULL,
	};

	NavSearchFunctions blockSearchFunctions = {
		(NavSearchCostToTargetFunction)		beaconBlockSearchCostToTarget,
		(NavSearchCostFunction)				beaconBlockSearchCost,
		(NavSearchGetConnectionsFunction)	beaconBlockSearchGetConnections,
		(NavSearchOutputPath)				beaconBlockSearchOutputPath,
		(NavSearchCloseNode)				NULL,
		(NavSearchGetNodeInfo)				NULL,
		(NavSearchReportClosedInfo)			NULL,
	};

	NavSearchFunctions beaconSearchFunctions = {
		(NavSearchCostToTargetFunction)		beaconSearchCostToTarget,
		(NavSearchCostFunction)				beaconSearchCost,
		(NavSearchGetConnectionsFunction)	beaconSearchGetConnections,
		(NavSearchOutputPath)				beaconSearchOutputPath,
		(NavSearchCloseNode)				NULL,
		(NavSearchGetNodeInfo)				beaconSearchGetNodeInfo,
		(NavSearchReportClosedInfo)			NULL,
	};

	NavSearchFunctions beaconSearchBlockFunctions = {
		(NavSearchCostToTargetFunction)		beaconSearchCostToTarget,
		(NavSearchCostFunction)				beaconSearchCost,
		(NavSearchGetConnectionsFunction)	beaconSearchGetConnectionsInBlock,
		(NavSearchOutputPath)				NULL,
		(NavSearchCloseNode)				NULL,
		(NavSearchGetNodeInfo)				beaconSearchGetNodeInfo,
		(NavSearchReportClosedInfo)			NULL,
	};

	NavSearchFunctions beaconSearchBlockInGalaxyFunctions = {
		(NavSearchCostToTargetFunction)		beaconBlockSearchCostToTarget,
		(NavSearchCostFunction)				beaconBlockSearchCost,
		(NavSearchGetConnectionsFunction)	beaconBlockSearchGetConnectionsInGalaxy,
		(NavSearchOutputPath)				beaconBlockSearchOutputPath,
		(NavSearchCloseNode)				NULL,
		(NavSearchGetNodeInfo)				NULL,
		(NavSearchReportClosedInfo)			NULL,
	};

	if(!s_wpTrackerSet && !disableWpTracking)
	{
		NavPathWaypoint *testWp;
		InitializeCriticalSection(&s_wpStashCS);
		eSetCreate(&s_wpTrackerSet, 10);

		testWp = createNavPathWaypoint();
		memTrackRegisterFreeCallback(testWp, beaconPathCheckWaypointIntegrity);
		destroyNavPathWaypoint(testWp);
	}

	beacon_state.searchFuncsCluster			= clusterSearchFunctions;
	beacon_state.searchFuncsGalaxy			= galaxySearchFunctions;
	beacon_state.searchFuncsBlock			= blockSearchFunctions;
	beacon_state.searchFuncsBeacon			= beaconSearchFunctions;
	beacon_state.searchFuncsBeaconInBlock	= beaconSearchBlockFunctions;
	beacon_state.searchFuncsBlockInGalaxy	= beaconSearchBlockInGalaxyFunctions;

	beacon_state.astarDataStandard = createAStarSearchData();
	initAStarSearchData(beacon_state.astarDataStandard);

	beacon_state.astarDataOther = createAStarSearchData();
	initAStarSearchData(beacon_state.astarDataOther);
}

#endif

#include "beaconPath_h_ast.c"
