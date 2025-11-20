/***************************************************************************
*     Copyright (c) 2012, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#pragma once

#include "Octree.h"

typedef struct WorldPathNode WorldPathNode;
typedef struct GoldenPathNode GoldenPathNode;
typedef struct AStarInfo AStarInfo;
typedef struct MinimapWaypoint MinimapWaypoint;
typedef struct ZoneMapLayer ZoneMapLayer;
typedef struct Octree Octree;
typedef struct OctreeEntry OctreeEntry;

AUTO_STRUCT;
typedef struct GoldenPathConfig
{
	const char *pchGoldenPathFX;

	const char *pchGoldenPathObstructedFX;

	const char *pchGoldenPathAreaFX;

	const char *pchGoldenPathAreaDeathFX;

	F32 fVerticalPathOffset;
} GoldenPathConfig;

AUTO_STRUCT;
typedef struct GoldenPathEdge
{
	U32 uOtherID; AST(KEY)
	GoldenPathNode *pOther; NO_AST
	F32 fCost;
	bool bIsTeleport;
} GoldenPathEdge;

AUTO_STRUCT;
typedef struct GoldenPathNode
{
	U32 uID; AST(KEY)
	Vec3 v3Pos;
	GoldenPathEdge **eaConnections;

	//Used for AStar search
	AStarInfo *astar_info; NO_AST
	
	bool bIsSecret;

	bool bCanBeObstructed;

	bool bHasBeenVisited; //Used for validation

	bool bIsTemporary;

	S32 iTeleportID;

	OctreeEntry octreeEntry; NO_AST
} GoldenPathNode;

AUTO_STRUCT;
typedef struct GoldenPathFXConnection
{
	//U32 uSourceNode;	AST(KEY)
	//U32 uTargetNode;
	//U32 fx;
	bool bIsSame;
	bool bIsTeleport;
	bool bIsObstructed;
	bool bIsArea;
	Vec3 v3ObstructedTarget;
	Vec3 v3Source;
	Vec3 v3Target;
	U32 *eaFX;
} GoldenPathFXConnection;

AUTO_STRUCT;
typedef struct GoldenPathStatus
{
	const char *pchGoldenPathFX; AST(POOL_STRING)

	const char *pchGoldenPathObstructedFX; AST(POOL_STRING)

	const char *pchGoldenPathAreaFX; AST(POOL_STRING)

	const char *pchGoldenPathAreaDeathFX; AST(POOL_STRING)

	//Is true when a path has been found to the target
	bool bFoundPath;

	//Is true when the player has arrived at their target
	bool bHasArrived;

	MinimapWaypoint *pTargetWaypoint;

	//This is an index into the array of nodes and FXs so that I can keep track of what part of the path has changed
	S32 iFXStartPoint;

	bool bIsPathObstructed;

	F32 fVerticalPathOffset;

	//This is the first path node on the path that lies inside of an area waypoint (if there is one)
	GoldenPathNode *pFirstNodeInArea;

	EARRAY_OF(GoldenPathFXConnection) eaPathFX;

	bool bFXHidden;

	Octree *pOctree; NO_AST
} GoldenPathStatus;

GoldenPathStatus *goldenPath_GetStatus();
GoldenPathNode *goldenPath_AddNode(Vec3 v3Pos);
void goldenPath_AddConnection(GoldenPathNode *pNode1, GoldenPathNode *pNode2, F32 fCost);
void goldenPath_OncePerFrame();

//This takes the passed-in earray, clears it, and fills it with all the nodes on the path.
//If bRemoveTempNodes is true, the temporary nodes added during smoothing won't be included.
void goldenPath_FillPathNodeList(GoldenPathNode ***peaNodes, bool bRemoveTempNodes);

//Returns true when the passed mission parameter corresponds to the target of the golden path
bool goldenPath_isPathingToMission(SA_PARAM_OP_VALID Mission *pMission);

//Returns true when no waypoint is available to make a path to
bool goldenPath_IsNoTargetAvailable();

//Queues a layer to have it's path node trackers updated
static void layerUpdatePathNodeTrackers(ZoneMapLayer *layer);

#include "AutoGen/gclGoldenPath_h_ast.h"