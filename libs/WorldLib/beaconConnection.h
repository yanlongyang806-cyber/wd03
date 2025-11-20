#pragma once
GCC_SYSTEM

#ifndef BEACONCONNECTION_H
#define BEACONCONNECTION_H

#include "wlBeacon.h"

typedef struct Beacon					Beacon;
typedef struct BeaconBlock				BeaconBlock;
typedef struct BeaconConnection			BeaconConnection;
typedef struct BeaconBlockConnection	BeaconBlockConnection;
typedef struct BeaconDynamicConnection	BeaconDynamicConnection;
typedef struct BeaconStatePartition		BeaconStatePartition;
typedef struct BeaconPartitionData		BeaconPartitionData;
typedef struct DefTracker				DefTracker;
typedef struct BeaconDynamicInfo		BeaconDynamicInfo;
typedef struct Entity					Entity;
typedef struct Array					Array;
typedef struct WorldInteractionEntry	WorldInteractionEntry;
typedef struct WorldCollisionEntry		WorldCollisionEntry;
typedef void* ReferenceHandle;

typedef enum DynConnState{
	DYN_CONN_CREATE,
	DYN_CONN_SHOW,
	DYN_CONN_HIDE,
	DYN_CONN_DESTROY,
} DynConnState;

BeaconPartitionData* beaconGetPartitionData(Beacon* b, int partitionId, int create);

void beaconConnSetSwapCallbacks(FrameLockedTimer* flt);
void beaconConnSetMovementStartCallback(BeaconConnMovementStartCallback func);
void beaconConnSetMovementIsFinishedCallback(BeaconConnMovementIsFinishedCallback func);
void beaconConnSetMovementResultCallback(BeaconConnMovementResultCallback func);

const char* beaconCurTimeString(int start);

void splitBeaconBlock(BeaconBlock* gridBlock);

F32 beaconMakeGridBlockCoord(F32 xyz);
void beaconMakeGridBlockCoords(Vec3 pos);

BeaconBlock* beaconGridBlockCreate(int partitionIdx);
BeaconBlock* beaconSubBlockCreate(int partitionIdx);
BeaconBlock* beaconGalaxyCreate(int partitionIdx, int galaxySet);
BeaconBlock* beaconClusterCreate(int partitionIdx);

BeaconConnection* createBeaconConnection(void);
void beaconGridBlockDestroy(BeaconBlock* block);
void beaconSubBlockDestroy(BeaconBlock* block);
void beaconGalaxyDestroy(BeaconBlock* block);
void beaconClusterDestroy(BeaconBlock* block);
void removeFirstGalaxyGalaxyOrSubBlock(BeaconStatePartition *partition, BeaconBlock *galaxy);
U32 beaconGetBeaconConnectionCount(void);

BeaconBlockConnection* beaconBlockConnectionCreate(BeaconBlock *src, BeaconBlock *dst);
void beaconBlockConnectionDestroy(BeaconBlockConnection* conn);

void destroyBeaconConnection(BeaconConnection* conn);

int beaconGetPassableHeight(int iPartitionIdx, Beacon* source, Beacon* destination, float* highest, float* lowest);

int beaconConnectsToBeaconByGround(int iPartitionIdx, Beacon* source, Beacon* target, Vec3 startPos, float maxRaiseHeight, Entity* ent, int createEnt, S32 *optionalOut, S32 *bidirOut);
int beaconConnectsToBeaconNPC(int iPartitionIdx, Beacon* source, Beacon* target);

int beaconCheckEmbedded(int iPartitionIdx, Beacon* source);

void beaconConnectToSet(Beacon* source);

int beaconMakeGridBlockHashValue(int x, int y, int z);

BeaconBlock* beaconGetGridBlockByCoords(BeaconStatePartition *partition, int x, int y, int z, int create);

void beaconSplitBlocksAndGalaxies(BeaconStatePartition *partition, int quiet);

void beaconClusterizeGalaxies(BeaconStatePartition *partition, int requireLegal, int quiet);

void beaconCreateAllClusterConnections(BeaconStatePartition *partition);

void beaconProcessCombatBeacons(int doGenerate, int doProcess);

void beaconClearAllBlockData(BeaconStatePartition *partition);

void beaconSubBlockCalcPos(BeaconBlock *subBlock);
void beaconSubBlockDestroyConnections(BeaconBlock *subBlock);
void beaconSubBlockMakeConnections(BeaconBlock *subBlock, int partitionId);
void beaconSubBlockRemakeConnections(BeaconBlock *subBlock);
U32 beaconGetBlockData(Beacon* b, int partitionIdx, BeaconBlock **subBlockOut, BeaconBlock **galaxyOut, BeaconBlock **clusterOut);

void beaconCheckBlocksNeedRebuild(int partition);

void beaconRebuildBlocks(int requireValid, int quiet, int partitionId);

typedef void (*DynConnCallback)(void *userptr, Beacon *bcn, BeaconConnection *conn, S32 raised, S32 index);

S32	beaconConnectionIsDisabled(Beacon *b, int partitionId, BeaconConnection *conn);

S32 beaconDynConnAllowedToSet(void);
void beaconDynConnProcessMesh(const Vec3 *verts,
							  S32 vert_count,
							  const S32 *tris,
							  S32 tri_count,
							  Vec3 min,
							  Vec3 max,
							  Mat4 world_mat,
							  DynConnCallback cb, 
							  void *userdata);
void beaconQueueDynConnCheck(DynConnState show_hide_change,
							 WorldCollObject *wco,
							 void* id,
							 int subId,
							 int partition);
void beaconHandleInteractionDestroyed(void *id);
void beaconDestroyDynamicInfo(BeaconDynamicInfo** dynamicInfoParam);
void beaconConnReInit(void);

void beaconClearDynConns(void);

void beaconProcessCombatBeacon(int iPartitionIdx, Beacon* source, S64 *groundTicks, S64 *raisedTicks);

LATELINK;
void beaconGatherDoors(DoorConn ***doors);

void beaconCheckInvalidSpawns(void);

U32 beaconConnectionGetNumWalksProcessed(void);
U32 beaconConnectionGetNumResults(U32 index);

BeaconBlockConnection *beaconBlockGetConnection(BeaconBlock *src, BeaconBlock *dst, int raised);
int beaconRemoveInvalidConnectionsFromArray(BeaconStatePartition *partition, Array *conns);
int beaconGalaxyRemoveInvalidConnectionsFromArray(Array *conns);
int beaconSubBlockRemoveInvalidConnectionsFromArray(Array* conns);

#endif
