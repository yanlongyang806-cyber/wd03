
#ifndef BEACONPATH_H
#define BEACONPATH_H
GCC_SYSTEM

#include "stdtypes.h"
#include "beacon.h"

typedef struct AStarSearchData	AStarSearchData;
typedef struct Beacon			Beacon;
typedef struct BeaconBlock		BeaconBlock;
typedef struct BeaconConnection	BeaconConnection;
typedef struct CommandQueue		CommandQueue;
typedef struct Entity			Entity;
typedef struct NavPathWaypoint	NavPathWaypoint;
typedef struct Position			Position;

AUTO_ENUM;
typedef enum NavPathConnectType {
	NAVPATH_CONNECT_GROUND,
	NAVPATH_CONNECT_JUMP,
	NAVPATH_CONNECT_FLY,
	NAVPATH_CONNECT_WIRE,
	NAVPATH_CONNECT_ENTERABLE,
	NAVPATH_CONNECT_ATTEMPT_SHORTCUT,
	
	NAVPATH_CONNECT_COUNT,
} NavPathConnectType;

typedef enum DataThreadState {
	DTS_NONE,
	DTS_INFG,
	DTS_TOBG,
	DTS_INBG,
	DTS_TOFG,
	DTS_FREE,
	DTS_COUNT,
} DataThreadState;

typedef struct DTS_Struct {
	DataThreadState				dts;
	const char*					dts_file[DTS_COUNT];
	int							dts_line[DTS_COUNT];
} DTS_Struct;

AUTO_STRUCT;
typedef struct NavPathWaypoint {
	Vec3						pos;
	Vec3						lastFailPos;
	Vec3						lastDirToPos;
	Beacon*						beacon;				NO_AST
	BeaconConnection*			connectionToMe;		NO_AST
	
	NavPathConnectType			connectType;

	CommandQueue*				commandsWhenPassed; NO_AST

	DTS_Struct					dts;				NO_AST

	U32							keepWaypoint : 1;
	U32							dontShortcut : 1;
	U32							gotStuck : 1;
	U32							jumped : 1;
	U32							attempted : 1;  // We tried doing what this told us to do (for jumps)
	U32							requestedRefine : 1;
	U32							targetWp : 1;
	U32							avoiding : 1;
	U32							attemptedShortcut : 1;
} NavPathWaypoint;

AUTO_STRUCT;
typedef struct NavPath {
	NavPathWaypoint**			waypoints;
	int							curWaypoint;
	U32							circular		: 1;
	U32							pingpong		: 1;
	U32							pingpongRev		: 1;
} NavPath;

AUTO_ENUM;
typedef enum NavSearchResultType {
	NAV_RESULT_CONTINUE,
	NAV_RESULT_BAD_POSITIONS,
	NAV_RESULT_NO_SOURCE_BEACON,
	NAV_RESULT_NO_TARGET_BEACON,
	NAV_RESULT_TARGET_UNREACHABLE, // pathfind "succeeded" but actually didn't get close to where it was supposed to go
	NAV_RESULT_BLOCK_ERROR,
	NAV_RESULT_NO_BLOCK_PATH,
	NAV_RESULT_NO_BEACON_PATH,
	NAV_RESULT_TIMEOUT,
	NAV_RESULT_CLUSTER_CONN_BLOCKED,
	NAV_RESULT_SUCCESS,
	NAV_RESULT_PARTIAL,		// pathfind was successful but did not make a path to the follow target
} NavSearchResultType;

typedef struct PathFindEntityData {
	U32			entref;
	int			partition;
	F32			jumpHeightCostMult;
	F32			jumpDistCostMult;
	F32			jumpCostInFeet;
	F32			maxJumpHeight;
	F32			maxJumpXZDiffSQR;
	F32			noFlyInitialYCostMult;
	F32			height;
	F32			turnRate;
	U32			noRaised			: 1;
	U32			canFly				: 1;
	U32			alwaysFly			: 1;
	U32			useEnterable		: 1;
	U32			pathLimited			: 1;
	U32			useAvoid			: 1;		// Lets the critter use avoid nodes as a last resort
} PathFindEntityData;

typedef enum BeaconQueuedPathfindPhase {
	QPF_Source,			// Take source pos, find source beacon
	QPF_Target,			//  ""  target "",   ""  target   ""
	QPF_Block,			// Block path search
	QPF_Beacon,			// Beacon path search
	QPF_DoorBlock,		// If door needed, block search
	QPF_DoorBeacon,		// If door needed, beacon search
} BeaconQueuedPathfindPhase;

typedef struct BeaconQueuedPathfind {
	int partitionIdx;

	PathFindEntityData pfEnt;

	BeaconQueuedPathfindPhase phase;
	Vec3 sourcePos;
	Vec3 targetPos;
	Beacon* sourceBeacon;
	Beacon* targetBeacon;

	// Output
	NavSearchResultType result;
	char* resultMsg;
	NavPath path;

	// Debug info
	const char* trivia;
	const char* file;
	int line;

	// State info
	F32 destinationHeight;
	U32 actualJumpHeight;
	U32 actualJumpHeightCostMult;
	Beacon* clusterConnTargetBeacon;
	U32 wantClusterConnect : 1;
	U32 losFailed : 1;
} BeaconQueuedPathfind;

//-------------------------------------------------------------------------------------------------

void beaconPathStartup(void);
void beaconCheckPathfindQueue(void);

typedef int (*IntEntRefCallback)(U32 entref);
typedef int (*BeaconEntRefCanFlyCallback)(U32 entref);
typedef int (*BeaconEntRefIsFlyingCallback)(U32 entref);
typedef int (*BeaconEntRefAlwaysFlyingCallback)(U32 entref);
typedef int (*BeaconEntRefNeverFlyingCallback)(U32 entref);
typedef F32 (*BeaconEntRefGetTurnRateCallback)(U32 entref);
typedef F32 (*BeaconEntRefGetJumpHeightCallback)(U32 entref);
typedef F32 (*BeaconEntRefGetJumpCostsCallback)(U32 entref);
void beaconSetPathFindPartitionCallback(IntEntRefCallback cb);
void beaconSetPathFindCanFlyCallback(BeaconEntRefCanFlyCallback cb);
void beaconSetPathFindIsFlyingCallback(BeaconEntRefCanFlyCallback cb);
void beaconSetPathFindAlwaysFlyingCallback(BeaconEntRefAlwaysFlyingCallback cb);
void beaconSetPathFindNeverFlyingCallback(BeaconEntRefNeverFlyingCallback cb);
void beaconSetPathFindTurnRateCallback(BeaconEntRefGetTurnRateCallback cb);
void beaconSetPathFindJumpHeightCallback(BeaconEntRefGetJumpHeightCallback cb);
void beaconSetPathFindJumpHeightMultCallback(BeaconEntRefGetJumpCostsCallback cb);
void beaconSetPathFindJumpDistMultCallback(BeaconEntRefGetJumpCostsCallback cb);
void beaconSetPathFindJumpCostCallback(BeaconEntRefGetJumpCostsCallback cb);

void beaconSetPathFindEntity(U32 entityRef, F32 maxJumpHeight, F32 height);
void beaconSetPathFindEntityUseAvoid(U32 allow);
void beaconSetPathFindEntityForceFly(U32 forceFly);
void beaconSetPathFindEntityNoRaised(U32 noRaised);

void beaconSetDebugClient(U32 entityRef);

void beaconSetPathFindEntityAsParameters(F32 maxJumpHeight, F32 height, S32 canFly, U32 useEnterable, F32 turnRate, int partitionId);

void dtsSetState_dbg(DTS_Struct *st, DataThreadState state, const char* file, int line);
#define dtsSetState(st, state) dtsSetState_dbg(st, state, __FILE__, __LINE__)

//-------------------------------------------------------------------------------------------------

#define navPathClear(path) navPathClearEx(path, __FILE__, __LINE__)
void navPathClearEx(NavPath *path, const char* filename, int line);
void navPathCopy(NavPath *src, NavPath *dst);
#define createNavPathWaypoint() createNavPathWaypointEx(__FILE__, __LINE__)
NavPathWaypoint* createNavPathWaypointEx(const char* file, int line);

#define destroyNavPathWaypoint(wp) destroyNavPathWaypointEx((wp), __FILE__, __LINE__)
void destroyNavPathWaypointEx(NavPathWaypoint* waypoint, const char* file, int line);

void copyNavPathWaypoint(NavPathWaypoint* src, NavPathWaypoint* dst);

void navPathAddTail(NavPath* path, NavPathWaypoint* wp);

void navPathAddHead(NavPath* path, NavPathWaypoint* wp);

NavPathWaypoint* navPathGetTargetWaypoint(NavPath* path);

// returns true if the next waypoint is the end of the current path
// reversing directions on a pingpong path and wrapping in a circular path,
//		both constitute end of path and will return true
int navPathGetNextWaypoint(NavPath* path, int* curWaypoint, int* pingpongRev);

int navPathUpdateNextWaypoint(NavPath* path);

//-------------------------------------------------------------------------------------------------

typedef int (*BeaconScoreFunction)(Beacon* src, BeaconConnection* conn, void *userData, F32 *minOut, F32 *maxOut);

enum{
	BEACONSCORE_FINISHED =	-1,
	BEACONSCORE_CANCEL =	-2,
};


Beacon* beaconGetClosestCombatBeacon(int iPartitionIdx, const Vec3 sourcePos, const Vec3 targetPos, F32 maxLOSRayRadius, Beacon* sourceBeacon, GCCBflags losFlags, S32* losFailed);

int beaconGalaxyPathExists(Beacon* source, Beacon* target, F32 maxJumpHeight, S32 canFly);

int beaconGetNextClusterWaypoint(Beacon* sourceBeacon, Beacon* targetBeacon, Vec3 outPos);

void beaconPathFindSimple(int iPartitionIdx, NavPath* path, Beacon* src, S32 maxPath, BeaconScoreFunction funcCheckBeacon, void *userdata);

void beaconPathFindSimpleStart(int iPartitionIdx, NavPath* path, const Vec3 pos, S32 maxPath, BeaconScoreFunction funcCheckBeacon, void *userdata);

#define beaconPathFind(iPartitionIdx, path, sourcePos, targetPos, trivia) beaconPathFindEx(iPartitionIdx, path, sourcePos, targetPos, trivia, __FILE__, __LINE__)
NavSearchResultType beaconPathFindEx(int iPartitionIdx, SA_PARAM_NN_VALID NavPath* path, const Vec3 sourcePos, const Vec3 targetPos, const char* trivia, const char* srcFile, int line);

NavSearchResultType beaconPathFindBeacon(int iPartitionIdx, NavPath *path, Beacon *source, Beacon *target);

void beaconPathInit(int forceUpdate);

void beaconClearBadDestinationsTable();

typedef bool (*BeaconPathDoorCallback)(Vec3 pos, Vec3 posOut);
void beaconPathSetDoorCallback(BeaconPathDoorCallback cb);

Array* beaconMakeSortedNearbyBeaconArray(int iPartitionIdx, const Vec3 sourcePos, float searchRadius);

void beaconConnectionSetBad(BeaconConnection* conn);
void beaconConnectionResetBad(BeaconConnection* conn);
U32 beaconConnectionGetTimeBad(BeaconConnection* conn);
void beaconConnectionClearBadness(BeaconStatePartition *partition);

LATELINK;
int aiShouldAvoidBeacon(U32 ref, Beacon* beacon, F32 height);

// posSrc is for point-to-beacon, such as initial beacon selection
LATELINK;
int aiShouldAvoidLine(U32 ref, Beacon* bcnSrc, const Vec3 posSrc, Beacon *bcnDst, F32 height, BeaconPartitionData * pStartBeaconPartitionData);

#endif