#ifndef BEACON_H
#define BEACON_H
GCC_SYSTEM

#include "Array.h"
//#include "position.h"
#include "timing.h"
#include "StashTable.h"

#include "beaconAStar.h"

#define MAX_BEACON_GALAXY_GROUP_COUNT			20
extern int beacon_galaxy_group_count;

typedef struct AIVolumeEntry			AIVolumeEntry;
typedef struct AStarSearchData			AStarSearchData;
typedef struct Beacon					Beacon;
typedef struct BeaconAvoidNode			BeaconAvoidNode;
typedef struct BeaconBlock				BeaconBlock;
typedef struct BeaconBlockConnection	BeaconBlockConnection;
typedef struct BeaconDynamicConnection	BeaconDynamicConnection;
typedef struct BeaconPathLogEntrySet	BeaconPathLogEntrySet;
typedef struct BeaconQueuedPathfind		BeaconQueuedPathfind;
typedef struct Capsule					Capsule;
typedef struct Entity					Entity;
typedef struct NavPath					NavPath;
typedef struct PathFindEntityData		PathFindEntityData;
typedef struct PSDKActor				PSDKActor;
typedef struct WorldColl				WorldColl;
typedef struct WorldCollObject			WorldCollObject;
typedef struct ZoneMapInfo				ZoneMapInfo;
typedef struct ESetImp*					ESet;

typedef U32 EntityRef;

extern Beacon** encounterBeaconArray;
extern char** invalidEncounterArray;
//extern Array trafficBeaconArray;

extern F32		combatBeaconGridBlockSize;
extern Array	combatBeaconArray;

extern int		gEnableRebuild;

extern StashTable namedSpatialMarkers;

typedef void (*BeaconPartitionCallback)(int partitionIdx);
typedef void (*BeaconPerPartitionCallback)(BeaconPartitionCallback func);

extern BeaconPathLogEntrySet *bcnPathLogSets[];

typedef struct BeaconStatePartition {
	int		id;
	int		needBlocksRebuilt;

	Array	combatBeaconGridBlockArray;
	Array	combatBeaconGalaxyArray [MAX_BEACON_GALAXY_GROUP_COUNT];
	Array	combatBeaconClusterArray;

	// For keeping synched with client debuggers
	int		nextSubBlockIndex;
	int*	subBlockIds;
	int		nextGalaxyIndex[MAX_BEACON_GALAXY_GROUP_COUNT];
	int*	galaxyIds[MAX_BEACON_GALAXY_GROUP_COUNT];
	int		nextClusterIndex;
	int*	clusterIds;

	StashTable combatBeaconGridBlockTable;

	// A table of BeaconDynamicInfo objects whose connections have been added/removed
	// since the last rebuild, used for performance reasons
	ESet changedInfos;

	U32 initialized : 1;
} BeaconStatePartition;

#define BEACON_MAX_DBG_IDS 15

typedef struct BeaconState {
	BeaconStatePartition   basePartition;
	BeaconStatePartition** partitions;		// partitions[0] = &basePartition

	int		beaconSearchInstance;
	int		galaxySearchInstance;

	BeaconQueuedPathfind *activePathfind;
	BeaconQueuedPathfind **queuedPathfinds;

	AStarSearchData *astarDataStandard;			// Used for BeaconPathFind Calls
	AStarSearchData *astarDataOther;			// Used for everything else

	NavSearchFunctions searchFuncsBlock;
	NavSearchFunctions searchFuncsBeacon;
	NavSearchFunctions searchFuncsBeaconInBlock;
	NavSearchFunctions searchFuncsBlockInGalaxy;
	NavSearchFunctions searchFuncsCluster;
	NavSearchFunctions searchFuncsGalaxy;
	
	BeaconPerPartitionCallback perPartitionCB;

	Vec3 debugBeaconPos[BEACON_MAX_DBG_IDS];
	Beacon *debugBeacons[BEACON_MAX_DBG_IDS];

	BeaconBlockConnection **connsForRealloc;
	
	int		mainThreadId;				// For thread-safety asserts	
	// I don't really feel like renaming these all the time
	U32		beaconDebugFlag1 : 1;		
	U32		beaconDebugFlag2 : 1;
	U32		beaconDebugFlag3 : 1;
	U32		beaconDebugFlag4 : 1;
	U32		beaconDebugFlag5 : 1;
	U32		beaconDebugFlag6 : 1;
	U32		beaconDebugFlag7 : 1;
	U32		beaconDebugFlag8 : 1;
	U32		forceDynConn : 1;			// Allow dyn conns to be done, even if beacons > 50000
} BeaconState;

extern BeaconState beacon_state;

typedef enum BeaconType {
	BEACONTYPE_BASIC = 0,
	BEACONTYPE_COMBAT,
	
	BEACONTYPE_COUNT,
} BeaconType;

typedef struct BeaconPartitionData {
	BeaconDynamicConnection**	disabledConns;

	BeaconAvoidNode**			avoidNodes;	

	// The block that the beacon is currently in (combat beacons only).
	BeaconBlock*				block;				NO_AST

	int							idx;
} BeaconPartitionData;

struct Beacon{
	Vec3						pos;			
	
	S32							proximityRadius;	NO_AST
	
	Array						gbConns;			NO_AST
	Array						rbConns;			NO_AST

	BeaconPartitionData**		partitions;			NO_AST
	
	F32							floorDistance;		NO_AST
	F32							ceilingDistance;	NO_AST

	// Pointer used by AStarSearch.  Should be NULL for all beacons.
	void*						astarInfo;			NO_AST
	// How many paths were blocked to me.
	S32							pathsBlockedToMe;	NO_AST

	// Do NOT use this user float unless you know what you're doing
	union {
		// Search instance for combat beacons.
		S32						searchInstance;		NO_AST

		F32						userFloatBcnizer;	NO_AST
	};
	
	union{
		// User defined stuff, use for temporary things.
		void*					userPointer;		NO_AST
		S32						userInt;			NO_AST
		F32						userFloat;			NO_AST
	};

	S32							globalIndex;
	char*						encounterStr;
	
	U32							drawnConnections		: 1;	NO_AST	// Debug flag for showing connections.
	U32							madeGroundConnections	: 1;	NO_AST	// Set if ground connections have been made (there might not be any).
	U32							madeRaisedConnections	: 1;	NO_AST	// Set if raised connections have been made (there might not be any).
	U32							killerBeacon			: 1;	NO_AST	// Set for traffic beacons that destroy the entity when it arrives at this beacon.
	U32							isValidStartingPoint	: 1;	NO_AST	// This beacon was created from a known valid starting point.
	U32							wasReachedFromValid		: 1;	NO_AST	// This beacon was reached during processing from a valid starting point.
	U32							noGroundConnections		: 1;	NO_AST	// Disable making ground connections in beacon processor.
	U32							groundConnsSorted		: 1;	NO_AST	// Used during clusterization for fast target lookups.
	U32							raisedConnsSorted		: 1;	NO_AST	// Used during clusterization for fast target lookups.
	U32							isEmbedded				: 1;	NO_AST	// Used during NPC beacon processing to avoid connecting to beacons that we know to be embedded in geometry.
	U32							NPCClusterProcessed		: 1;	NO_AST	// Used to keep track of whether this beacon has been processed in NPC Cluster finding.
	U32							NPCNoAutoConnect		: 1;	NO_AST	// Disable automatically making a connection out of this island.
	U32							pathLimitedAllowed		: 1;	NO_AST	// Entities that are set as path limited are allowed to use this beacon.
	U32							noDynamicConnections	: 1;	NO_AST	// Connections to or from beacons with this set do not generate dynconns
	U32							isSpecial				: 1;	NO_AST	// Denotes a special beacon, meaning it's related to a spawn point, patrol point or interactable
};

#define BEACON_CONN_FLOOR_BITS		4
#define BEACON_CONN_FLOOR_DIST		(1 << BEACON_CONN_FLOOR_BITS)

typedef struct BeaconConnection {
	Beacon*				destBeacon;
	union {
		U16				minHeight;
		struct {
			U16			optional : 1;
		} gflags;
	};
	U16					maxHeight;

	// XZ only for raised connections
	F16					distance;

	// Only 16 bits for flags or other data
	U16					groundDist	: BEACON_CONN_FLOOR_BITS;
	U16					raised		: 1;
	U16					wasBadEver	: 1;
	U16					disabled	: 1;
} BeaconConnection;

STATIC_ASSERT(sizeof(BeaconConnection)==8 + sizeof(Beacon*));

void beaconFix();

BeaconPartitionData* beaconGetPartitionData(Beacon* b, int partitionId, int create);

typedef void BeaconForEachBlockCallback(Array* beaconArray, void* userData);

void beaconForEachBlockIntPartition(int partitionIdx, const Vec3 pos, F32 rx, F32 ry, F32 rz, BeaconForEachBlockCallback func, void* userData);
void beaconForEachBlockBoundsIntPartition(int partitionIdx, const Vec3 posLow, const Vec3 posHigh, BeaconForEachBlockCallback func, void* userData);
void beaconForEachBlock(BeaconStatePartition *partition, const Vec3 pos, F32 rx, F32 ry, F32 rz, BeaconForEachBlockCallback func, void* userData);
void beaconForEachBlockBounds(BeaconStatePartition *partition, const Vec3 posLow, const Vec3 posHigh, BeaconForEachBlockCallback func, void* userData);

Beacon* beaconGetNearestBeacon(int partitionIdx, const Vec3 pos);
int beaconCompareUserfloat(const Beacon** b1, const Beacon** b2);

void beaconAddCritterSpawn(const Vec3 pos, char* encounterStr);
void beaconAddUsefulPoint(const Vec3 pos, U32 isSpecial);

// Clears all beacon data
void beaconClearBeaconData(void);
void beaconDestroyObjects(void);

// This will both load the encounter system and call beaconAddSpawnPosition on each spawn position!
// And all without callbacks
LATELINK;
void beaconGatherSpawnPositions(int spawn_only);

void beaconServerCreateSpaceBeacons(void);

// Called from the above - defined in beaconServer.c
void beaconAddSpawnLocation(const Vec3 pos, int noGroundConnections, U32 isSpecial);

// Allows dynamic connections to be disabled even if beacon count is high
void beaconSetForceDynConn(int force);

// forces an immediate check on dynamic connections that are queued to be tested 
void beaconCheckDynConnQueue(void);

// Force block rebuild... only for debugging purposes
void beaconRebuildBlocks(int requireValid, int quiet, int partitionId);
void beaconCheckBlocksNeedRebuild(int partitionId);

// Finds the position of the next door/connection between the source and destination
// If no connection is found, return the destination coordinates
// Returns TRUE if a connection was found
bool beaconFindNextConnection(int iPartitionIdx, EntityRef ref, Vec3 src, Vec3 dest, Vec3 result, const char* triviaStr);

typedef enum BeaconAvoidCheckBits {
	BEACON_AVOID_POINT	= BIT(0),
	BEACON_AVOID_LINE	= BIT(1),
} BeaconAvoidCheckBits;

// Avoid stuff
typedef struct BeaconAvoidNode {
	Beacon*					beacon;
	Entity*					e;
	AIVolumeEntry*			entry;
	BeaconAvoidCheckBits	avoidCheckBits;
	int						partitionIdx;
	BeaconAvoidNode***		onList;
} BeaconAvoidNode;

// Linecheck implies the beacon wasn't literally inside the avoid, but close enough to cause avoid on paths
BeaconAvoidNode* beaconAddAvoidNode(Beacon* beacon, Entity* e, AIVolumeEntry *entry, int partitionIdx, BeaconAvoidCheckBits bits, BeaconAvoidNode ***onList);
void beaconDestroyAvoidNode_CalledFromAI(BeaconAvoidNode* node);
void beaconDestroyAvoidNode_CalledFromBeacon(BeaconAvoidNode* node);

typedef enum GCCBflags { 
	GCCB_IGNORE_LOS  			= 1<<0, // Don't even try to get line of sight (won't work with no sourceBeacon).
	GCCB_PREFER_LOS	 			= 1<<1, // Try to get line of sight on sourcePos, fall back to no line of sight (if sourceBeacon).
	GCCB_REQUIRE_LOS 			= 1<<2, // Only return beacon if it has los on sourcePos.
	GCCB_IF_ANY_LOS_ONLY_LOS	= 1<<3, // Prefer los, but if you find *any* los beacon that fails to connect to source beacon, don't try no-los beacons.

	GCCB_STARTS_IN_AVOID		= 1<<4,
} GCCBflags;

int beaconGalaxyPathExists(Beacon* source, Beacon* target, F32 maxJumpHeight, S32 canFly);
Beacon* beaconGetClosestCombatBeacon(int iPartitionIdx, const Vec3 sourcePos, const Vec3 targetPos, F32 maxLOSRayRadius, Beacon* sourceBeacon, GCCBflags losFlags, S32* losFailed);


// return false if you want to pass over the given beacon
typedef bool (*FilterBeaconFunc)(const Beacon* beacon, void* userData);
// Returns an EArray of Beacons. 
void beaconGetNearbyBeacons(SA_PARAM_NN_VALID Beacon ***peaBeacons,
							const Vec3 sourcePos, 
							F32 searchRadius, 
							SA_PARAM_OP_VALID FilterBeaconFunc callback, 
							SA_PARAM_OP_VALID void* userData );


typedef struct WorldColl WorldColl;
typedef struct WorldCollCollideResults WorldCollCollideResults;
S32 beaconRayCollide(int iPartitionIdx,
					 WorldColl* wc,
					 const Vec3 source,
					 const Vec3 target,
					 U32 shapeGroups,
					 WorldCollCollideResults* resultsOut);

S32 beaconCapsuleCollide(	WorldColl* wc, 
							const Vec3 source, 
							const Vec3 target, 
							U32 shapeGroups, 
							WorldCollCollideResults* resultsOut);

S32 beaconCapsuleCollideCheck(	WorldColl* wc, 
							  const Capsule *cap, 
							  const Vec3 source, 
							  U32 shapeGroups, 
							  WorldCollCollideResults* resultsOut);

// Log entry functionality

AUTO_STRUCT;
typedef struct BeaconPathLogEntry {
	EntityRef	entityRef;

	Vec3		sourcePos;
	Vec3		targetPos;

	S64			totalCycles;
	S64			findSource;
	S64			findTarget;
	S64			findPath;
	U32			abs_time;

	int			searchResult;
	Vec3		sourceBeaconPos;
	Vec3		targetBeaconPos;

	const char  *ownerFile;			AST(UNOWNED)
	int			ownerLine;

	char		*triviaStr;

	F32			jumpHeight;
	U32			canFly : 1;
	U32			success : 1;
} BeaconPathLogEntry;

AUTO_STRUCT;
typedef struct BeaconPathLogEntrySet {
	BeaconPathLogEntry	**entries;
	int					cur;
	int					total;
	int					max_size;
} BeaconPathLogEntrySet;

extern BeaconPathLogEntrySet *bcnPathLogSets[];

S32 beaconPathLogSetsGetSize();
S64 beaconPathLogGetSetLimitMin(S32 set);
S64 beaconPathLogGetSetLimitMax(S32 set);

// This function tries to determine whether the given position is a valid place
// for an entity to be, i.e. not stuck in terrain, out of bounds, etc.
bool beaconIsPositionValid(int iPartitionIdx, const Vec3 pos, const Capsule *cap);

void beaconAddNoDynConnBox(const Mat4 world_mat, const Vec3 local_min, const Vec3 local_max, const void *ptrId);
void beaconAddNoDynConnSphere(const Vec3 pos, F32 radius, const void *ptrId);
void beaconRemoveNoDynConnVol(const void* ptrId);

#endif
