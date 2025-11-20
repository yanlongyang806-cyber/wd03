
#ifndef BEACONPRIVATE_H
#define BEACONPRIVATE_H
GCC_SYSTEM

#include "stdtypes.h"
#include <float.h>
#include "beacon.h"

#include "Array.h"
#include "error.h"
#include "beaconDebug.h"
#include "StashTable.h"

typedef struct BeaconProcessConfig		BeaconProcessConfig;
typedef struct NavPathWaypoint			NavPathWaypoint;
typedef struct BeaconBlock				BeaconBlock;
typedef struct BeaconAvoidNode			BeaconAvoidNode;
typedef struct NetLink					NetLink;
typedef struct BeaconMapMetaData		BeaconMapMetaData;
typedef struct ZoneMapInfo				ZoneMapInfo;
typedef struct BeaconBlockConnection	BeaconBlockConnection;

typedef struct BBCArray
{
	int size;
	int maxsize;
	BeaconBlockConnection **storage;
} BBCArray;

typedef struct BBArray
{
	int size;
	int maxsize;
	BeaconBlock **storage;
} BBArray;

typedef struct BeaconBlock {
	int					globalIndex;

	// Search Data
	F32					searchY;
	void*				astarInfo;
	U32					searchInstance;

	union{
		BeaconBlock*	parentBlock;
		BeaconBlock*	cluster;
	};

	Vec3				pos;
	BeaconBlock*		galaxy;
	int					galaxySet;

	Array				beaconArray;
	Array				subBlockArray;

	union
	{
		Array				gbbConns;
		BBCArray			gbbcConns;
	};
	union
	{
		Array				rbbConns;
		BBCArray			grbbConns;
	};
	union
	{
		Array				bbIncoming;
		BBArray				bbIncUseful;
	};

	int					partitionIdx;
	U32					isGridBlock				: 1;
	U32					isGalaxy				: 1;
	U32					isCluster				: 1;
	U32					isSubBlock				: 1;
	U32					madeConnections			: 1;
	U32					isGoodForSearch			: 1;
	U32					dirty					: 1;
	U32					galaxyPruneChecking		: 1;
	U32					galaxyPrunerHasSpecial	: 1;
} BeaconBlock;

typedef struct BeaconBlockConnection {
	BeaconBlock*				srcBlock;
	BeaconBlock*				destBlock;
	union {
		void**						conns;			// Valid only for galaxy levels 1+ (i.e. raised connections) and subblocks
		BeaconBlockConnection**		bccs;
		BeaconConnection**			ccs;
	};
	U16							minJumpHeight;
	S16							minHeight;
	S16							maxHeight;
	U16							connCount;		// Number of BeaconConnection-s between these two blocks
	U16							blockCount;
	// 16 bits for flags
	U16							raised	: 1;
} BeaconBlockConnection;

typedef struct BeaconClusterConnection {
	BeaconBlock*				srcCluster;
	BeaconBlock*				dstCluster;
	BeaconDynamicConnection*	dynConnToTarget;

	struct {
		Vec3			pos;
		Beacon*			beacon;
	} source;

	struct {
		Vec3			pos;
		Beacon*			beacon;
	} target;
} BeaconClusterConnection;

extern F32 beaconGalaxyGroupJumpIncrement;
extern NetLink* beacon_debug_link; // Declared in beaconclient.c

//----------------------------------------------------------------
// beaconFile.c
//----------------------------------------------------------------

typedef struct BeaconProcessInfo	BeaconProcessInfo;
typedef struct BeaconDiskSwapBlock	BeaconDiskSwapBlock;

typedef struct BeaconProcessState {
	const char* 				titleMapName;
	
	char*						beaconFileName;
	char*						beaconDateFileName;
	char*						beaconInvalidFileName;

	U32							beaconFileTime;

	U32							latestDataFileTime;
	char*						latestDataFileName;

	BeaconMapMetaData			*fileMetaData;
	BeaconMapMetaData			*mapMetaData;

	Vec3						world_min_xyz;
	Vec3						world_max_xyz;

	int							groupDefCount;

	BeaconProcessInfo*			infoArray;
	int*						processOrder;
	
	Vec3						entityPos;
	
	StashTable					stCachedModelData;
	
	int							memoryAllocated;
	int							legalCount;
	BeaconDiskSwapBlock*		curDiskSwapBlock;
	int							nextDiskSwapBlockIndex;
	int							validStartingPointMaxLevel;

	S32							is_new_file : 1;
	S32							bcn_checkedout : 1;
	S32							bcn_date_checkedout : 1;
	S32							bcn_invalid_checkedout : 1;
	S32							isSpaceMap : 1;
} BeaconProcessState;

extern BeaconProcessState beacon_process;

F32 beaconSnapPosToGround(int iPartitionIdx, Vec3 posInOut);

Beacon* addCombatBeacon(const Vec3 beaconPos,
						S32 silent,
						S32 snapToGround,
						S32 destroyIfTooHigh,
						U32 isSpecial);
						
void beaconProcessSetTitle(F32 progress, const char* sectionName);
void beaconProcessUndoCheckouts();
int beaconCheckoutBeaconFiles(int noFileCheck, int removeOldFiles);
S32 beaconEnsureFilesExist(S32 noFileCheck);
void beaconProcessSetConsoleCtrlHandler(int on);
BeaconProcessConfig* beaconServerGetProcessConfig(void);

//----------------------------------------------------------------
// beaconConnection.c
//----------------------------------------------------------------

typedef struct BeaconDynamicInfo BeaconDynamicInfo;

typedef struct BeaconDynamicConnectionPartition {
	S32							blockedCount;
	int							idx;
} BeaconDynamicConnectionPartition;

typedef struct BeaconDynamicConnection {
	BeaconDynamicInfo**			infos;

	Beacon*						source;
	BeaconConnection*			conn;
	S32							raised;
	
	BeaconDynamicConnectionPartition **partitions;
} BeaconDynamicConnection;

typedef struct BeaconDynamicInfoPartition {
	int							idx;

	U32							connsEnabled : 1;
	// Stores the state when the last rebuild happened
	U32							connsLastRebuild : 1;		
} BeaconDynamicInfoPartition;

typedef struct BeaconDynamicInfo {
	void*						id;
	int							subId;
	BeaconDynamicConnection**	conns;

	BeaconDynamicInfoPartition** partitions;
} BeaconDynamicInfo;

typedef struct BeaconDynamicInfoList {
	void*						id;
	BeaconDynamicInfo**			infos;
} BeaconDynamicInfoList;

#define BEACON_PARTITION_LIMIT  200000

BeaconStatePartition* beaconStatePartitionGet(int partitionId, int create);

void beaconInitArray_dbg(Array* array, U32 count, const char* file, int line);
void beaconInitCopyArray_dbg(Array* array, Array* source, const char* file, int line);
void beaconBlockInitCopyArray_dbg(Array* array, Array* source, const char* file, int line);
void beaconBlockInitArray_dbg(Array* array, U32 count, const char* file, int line);

#define beaconInitArray(array, count)		beaconInitArray_dbg(array, count, __FILE__, __LINE__)
#define beaconInitCopyArray(array, source)	beaconInitCopyArray_dbg(array, source, __FILE__, __LINE__)
#define beaconBlockInitCopyArray(array, source)	beaconBlockInitCopyArray_dbg(array, source, __FILE__, __LINE__)
#define beaconBlockInitArray(array, count)	beaconBlockInitArray_dbg(array, count, __FILE__, __LINE__)

int beaconNPCClusterTraverser(Beacon* beacon, int* curSize, int maxSize);

// You probably shouldn't be calling this
typedef void (*BeaconBlockTraverseCallback)(BeaconBlock* block, void* userdata);
void beaconTraverseBlocks(BeaconStatePartition *partition, const Vec3 posMin, const Vec3 posMax, F32 buffer, BeaconBlockTraverseCallback func, void* userdata);

int cmpDynToConn(const BeaconDynamicConnection *dynConn, const BeaconConnection *conn);

//----------------------------------------------------------------
// beacon.c
//----------------------------------------------------------------

void beaconClearBeaconData(void);

Beacon* createBeacon(void);
void destroyCombatBeacon(Beacon* beacon);
void beaconPartitionDestroy(BeaconStatePartition *partition);
void beaconPartitionDataDestroy(Beacon* b, BeaconPartitionData *partition);

F32 beaconGetDistanceYToCollision(int iPartitionIdx, const Vec3 pos, F32 dy);
F32 beaconGetJitteredPointFloorDistance(int iPartitionIdx, const Vec3 posParam);
F32 beaconGetCapFloorDistance(const Vec3 posParam);
F32 beaconGetFloorDistance(int iPartitionIdx, Beacon* b);
F32 beaconGetPointCeilingDistance(const Vec3 posParam);
F32 beaconGetCeilingDistance(int iPartitionIdx, Beacon* b);

#endif
