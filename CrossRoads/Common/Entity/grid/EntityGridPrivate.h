#pragma once
GCC_SYSTEM

/***************************************************************************
*     Copyright (c) 2005-Present, Cryptic Studios
*     All Rights Reserved
*     Confidential Property of Cryptic Studios
***************************************************************************/

#if !GAMESERVER && !GAMECLIENT
	#error No Movement code allowed here.
#endif

//#define ENTITY_GRID_DEBUG 1
#include "Entity.h"

typedef struct Entity					Entity;
typedef struct EntityGrid					EntityGrid;
typedef struct EntityGridNode				EntityGridNode;
typedef struct EntityGridNodeEntry			EntityGridNodeEntry;
typedef struct EntityGridCell				EntityGridCell;
typedef struct EntityGridCellEntryArray		EntityGridCellEntryArray;
typedef struct EntityGridCellEntryArraySlot	EntityGridCellEntryArraySlot;

typedef struct EntityGridNodeEntry {
	EntityGridCellEntryArraySlot*	slot;
	EntityGridCell*					cell;
} EntityGridNodeEntry;

typedef struct EntityGridNode {
	EntityGrid*			grid;
	Entity*				ent;
	EntityFlags			entFlags;
	Vec3				pos;
	F32					radius;
	S32					entries_count;
	S32					radius_hi_bits;
	S32					radius_lo;
	S32					use_small_cells;
	IVec3				grid_pos;
	EntityGridNodeEntry	entries[100];
} EntityGridNode;

typedef struct EntityGridCellEntryArraySlot {
	Entity*					ent;
	EntityFlags				entFlags;
	EntityGridNodeEntry*	entry;
	
	#if ENTITY_GRID_DEBUG
		EntityGridNode*		node;
	#endif
} EntityGridCellEntryArraySlot;

typedef struct EntityGridCellEntryArray {
	EntityGridCellEntryArray*		next;
	
	S32								count;
	EntityGridCellEntryArraySlot	slot[10];
} EntityGridCellEntryArray;

typedef struct EntityGridCell {
	S32 mid[3];

	struct {
		EntityGridCell* xyz[2][2][2];
		EntityGridCell* xy[2];
		EntityGridCell* yz[2];
		EntityGridCell* xz[2];
	} children;
	
	S32 cellSize;

	EntityGridCellEntryArray* entries;
} EntityGridCell;

typedef struct EntityGrid {
	EntityGridCell	rootCell;
	S32				nodeCount;
	S32				nodeEntryCount;
} EntityGrid;

typedef struct EntityGridSet {
	int iPartitionIdx;
	EntityGrid entityGrid[4];
} EntityGridSet;

extern EntityGridSet **entityGrids;

extern EntityGridNode *egdn;

EntityGrid* egFromPartition(int iPartitionIdx, int grid);

#if ENTITY_GRID_DEBUG
	typedef void(*EntityGridVerifyCallback)(void* owner, EntityGridNode* node);

	void egVerifyGrid(int iPartitionIdx, int gridIndex, EntityGridVerifyCallback callback);
#endif

EntityGridNode* createEntityGridNode(Entity* owner, F32 radiusF32, int use_small_cells);
void destroyEntityGridNode(EntityGridNode* node);
void egUpdateNodeRadius(EntityGridNode* node, F32 radiusF32, int use_small_cells);

void egVerifyAllGrids(EntityGridNode* excludeNode);

void egUpdate(	int iPartitionIdx,
				int grid, 
				EntityGridNode* node,
				EntGridPosCopy* copyOut,
				S32* copyUpdatedOut,
				const Vec3 posF32,
				int forceUpdate);

void egRemove(EntityGridNode* node);
int egGetNodesInRange(int iPartitionIdx, int grid, const Vec3 posF32, Entity** nodeArray);
// only place using this wants dist and flags...
//int egGetNodesInRangeWithSlotSize(int grid, Vec3 posF32, Entity** nodeArray, int size);
int egGetNodesInRangeSaveDist(int iPartitionIdx, int grid, const Vec3 posF32, F32 maxDist, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, EntAndDist** nodeArray, int* size, int* maxSize, Entity* sourceEnt);
int egGetNodesInRangeWithDistAndFlags(int iPartitionIdx, int grid, const Vec3 posF32, F32 dist, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, Entity** nodeArray, Entity* sourceEnt);

