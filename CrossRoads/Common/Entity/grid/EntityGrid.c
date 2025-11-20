
#include <stdio.h>

#include "EntityGrid.h"
#include "EntityGridPrivate.h"


#include "MemoryPool.h"

#ifdef GAMESERVER
#include "gslPartition.h"
#endif

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_GameSystems););

EntityGridSet **entityGrids = NULL;

EntityGrid* egFromPartition(int iPartitionIdx, int grid)
{
	int i;
	for(i=eaSize(&entityGrids)-1; i>=0; i--)
		if(entityGrids[i]->iPartitionIdx==iPartitionIdx)
			return &(entityGrids[i]->entityGrid[grid]);
	return NULL;
}

void egInitGrid(EntityGrid* grid, int maxSize){
	int realMax = 1;
	
	assert(vec3IsZero(grid->rootCell.mid));

	while(maxSize){
		maxSize >>= 1;
		realMax <<= 1;
	}
	
	grid->rootCell.mid[0] = 0;
	grid->rootCell.mid[1] = 0;
	grid->rootCell.mid[2] = 0;
	
	grid->rootCell.cellSize = realMax * 2;
}

MP_DEFINE(EntityGridCellEntryArray);

static EntityGridCellEntryArray* __fastcall createEntityGridCellEntryArray(){
	EntityGridCellEntryArray* array;
	
	if(!MP_NAME(EntityGridCellEntryArray)){
		MP_CREATE(EntityGridCellEntryArray, 1000);
	
		mpSetMode(MP_NAME(EntityGridCellEntryArray), 0);
	}
	
	array = MP_ALLOC(EntityGridCellEntryArray);
	
	array->count = 0;

	return array;
}

MP_DEFINE(EntityGridCell);

static EntityGridCell* __fastcall createEntityGridCell(int cellSize, int midx, int midy, int midz){
	EntityGridCell* cell;
	
	if(!MP_NAME(EntityGridCell)){
		MP_CREATE(EntityGridCell, 1000);

		mpSetMode(MP_NAME(EntityGridCell), 0);
	}
	
	cell = MP_ALLOC(EntityGridCell);
	
	cell->mid[0] = midx;
	cell->mid[1] = midy;
	cell->mid[2] = midz;
	
	cell->cellSize = cellSize;
	
	ZeroStruct(&cell->children);

	return cell;
}

MP_DEFINE(EntityGridNode);

EntityGridNode* createEntityGridNode(Entity* entity, F32 radiusF32, int use_small_cells){
	EntityGridNode* node;
	
	MP_CREATE(EntityGridNode, 10);
	
	node = MP_ALLOC(EntityGridNode);
	
	node->ent = entity;
	node->entFlags = entGetFlagBits(entity);

	egUpdateNodeRadius(node, radiusF32, use_small_cells);
	
	return node;
}

void destroyEntityGridNode(EntityGridNode* node){
	if(node){
		void egRemove(EntityGridNode* node);

		egRemove(node);
		
		MP_FREE(EntityGridNode, node);
	}
}

void egUpdateNodeRadius(EntityGridNode* node, F32 radiusF32, int use_small_cells){
	if(*(int*)&node->radius != *(int*)&radiusF32){
		int bits = 0;
		int radius = radiusF32;
	
		node->radius = radius;

		while(radius > 1){
			bits++;
			radius >>= 1;
		}
		
		if(node->radius > (1 << bits)){
			bits++;
		}
		
		if(use_small_cells)
		{
			radius = (1 << bits) - (1 << (bits - 2));

			if(node->radius < radius)
			{
				bits--;
				node->radius_lo = (1 << bits) + (1 << (bits - 1));
 			}
 			else
 			{
				node->radius_lo = (1 << bits);
			}
		}
		else
		{
			node->radius_lo = (1 << bits);
		}

		node->radius_hi_bits = bits;
		
		node->use_small_cells = use_small_cells;
	}
}

static struct {
	int						smallCellSize;
	int						largeCellSize;
	EntityGridNode*			node;
	Entity*				ent;
	EntityFlags				entFlags;
	EntityGridNodeEntry*	entries;
	int						entries_count;
	int						lo[3];
	int						hi[3];
} addNode;

static void __fastcall egAddToCellHelper(EntityGridCell* cell){
	EntityGridNodeEntry* entry = addNode.entries + addNode.entries_count;
	EntityGridCellEntryArray* head = cell->entries;
	U32 count;
	
	addNode.entries_count++;
	
	assert(addNode.entries_count <= ARRAY_SIZE(addNode.node->entries));

	if(!head){
		head = cell->entries = createEntityGridCellEntryArray();
		head->next = NULL;
	}
	else if(head->count == ARRAY_SIZE(head->slot)){
		head = createEntityGridCellEntryArray();

		head->next = cell->entries;
		cell->entries = head;
	}

	// Add to cell's list.
	
	count = head->count++;
	
	if(count < ARRAY_SIZE(head->slot)){
		head->slot[count].entry = entry;
		head->slot[count].ent = addNode.ent;
		head->slot[count].entFlags = addNode.entFlags;
	
		#if ENTITY_GRID_DEBUG
			head->slot[count]->node = addNode.node;
		#endif
	}

	entry->cell = cell;
	entry->slot = head->slot + count;
	//entry->array = head;
	//entry->arrayIndex = count;
}

static void __fastcall egAddToCell(EntityGridCell* cell, int x0, int x1, int y0, int y1, int z0, int z1){
	int cellSize = cell->cellSize;
	
	if(cellSize == addNode.smallCellSize){
		egAddToCellHelper(cell);
		return;
	}
	else if(cellSize == addNode.largeCellSize){
		int xs = !x0 && x1;
		int ys = (!y0 && y1) ? 2 : 0;
		int zs = (!z0 && z1) ? 4 : 0;
		int halfIndex = xs | ys | zs;
		EntityGridCell* newCell;
		
		switch(halfIndex){
			case 3:
				newCell = cell->children.xy[z1];
				if(!newCell){
					newCell = cell->children.xy[z1] = createEntityGridCell(0,0,0,0);
				}
				egAddToCellHelper(newCell);
				return;
			case 5:
				newCell = cell->children.xz[y1];
				if(!newCell){
					newCell = cell->children.xz[y1] = createEntityGridCell(0,0,0,0);
				}
				egAddToCellHelper(newCell);
				return;
			case 6:
				newCell = cell->children.yz[x1];
				if(!newCell){
					newCell = cell->children.yz[x1] = createEntityGridCell(0,0,0,0);
				}
				egAddToCellHelper(newCell);
				return;
			case 7:
				egAddToCellHelper(cell);
				return;
		}
	}
		
	{
		EntityGridCell* (*children)[2][2] = cell->children.xyz;
		EntityGridCell* child;
		int mid[3];
		
		copyVec3(cell->mid, mid);

		// Go into the child cells.
		
		if(!x0){
			if(!y0){
				if(!z0){
					child = children[0][0][0];
					
					if(!child){
						child = children[0][0][0] = createEntityGridCell(	cellSize / 2,
																		mid[0] - cellSize / 4,
																		mid[1] - cellSize / 4,
																		mid[2] - cellSize / 4);
					}

					egAddToCell(child,
								addNode.lo[0] >= child->mid[0], x1 ? 1 : addNode.hi[0] >= child->mid[0],
								addNode.lo[1] >= child->mid[1], y1 ? 1 : addNode.hi[1] >= child->mid[1],
								addNode.lo[2] >= child->mid[2], z1 ? 1 : addNode.hi[2] >= child->mid[2]);
				}

				if(z1){
					child = children[0][0][1];

					if(!child){
						child = children[0][0][1] = createEntityGridCell(	cellSize / 2,
																		mid[0] - cellSize / 4,
																		mid[1] - cellSize / 4,
																		mid[2] + cellSize / 4);
					}

					egAddToCell(child,
								addNode.lo[0] >= child->mid[0], x1 ? 1 : addNode.hi[0] >= child->mid[0],
								addNode.lo[1] >= child->mid[1], y1 ? 1 : addNode.hi[1] >= child->mid[1],
								z0 ? addNode.lo[2] >= child->mid[2] : 0, addNode.hi[2] >= child->mid[2]);
				}
			}
			
			if(y1){
				if(!z0){
					child = children[0][1][0];

					if(!child){
						child = children[0][1][0] = createEntityGridCell(	cellSize / 2,
																		mid[0] - cellSize / 4,
																		mid[1] + cellSize / 4,
																		mid[2] - cellSize / 4);
					}

					egAddToCell(child,
								addNode.lo[0] >= child->mid[0], x1 ? 1 : addNode.hi[0] >= child->mid[0],
								y0 ? addNode.lo[1] >= child->mid[1] : 0, addNode.hi[1] >= child->mid[1],
								addNode.lo[2] >= child->mid[2], z1 ? 1 : addNode.hi[2] >= child->mid[2]);
				}

				if(z1){
					child = children[0][1][1];

					if(!child){
						child = children[0][1][1] = createEntityGridCell(	cellSize / 2,
																		mid[0] - cellSize / 4,
																		mid[1] + cellSize / 4,
																		mid[2] + cellSize / 4);
					}

					egAddToCell(child,
								addNode.lo[0] >= child->mid[0], x1 ? 1 : addNode.hi[0] >= child->mid[0],
								y0 ? addNode.lo[1] >= child->mid[1] : 0, addNode.hi[1] >= child->mid[1],
								z0 ? addNode.lo[2] >= child->mid[2] : 0, addNode.hi[2] >= child->mid[2]);
				}
			}
		}
		
		if(x1){
			if(!y0){
				if(!z0){
					child = children[1][0][0];

					if(!child){
						child = children[1][0][0] = createEntityGridCell(	cellSize / 2,
																		mid[0] + cellSize / 4,
																		mid[1] - cellSize / 4,
																		mid[2] - cellSize / 4);
					}

					egAddToCell(child,
								x0 ? addNode.lo[0] >= child->mid[0] : 0, addNode.hi[0] >= child->mid[0],
								addNode.lo[1] >= child->mid[1], y1 ? 1 : addNode.hi[1] >= child->mid[1],
								addNode.lo[2] >= child->mid[2], z1 ? 1 : addNode.hi[2] >= child->mid[2]);
				}

				if(z1){
					child = children[1][0][1];

					if(!child){
						child = children[1][0][1] = createEntityGridCell(	cellSize / 2,
																		mid[0] + cellSize / 4,
																		mid[1] - cellSize / 4,
																		mid[2] + cellSize / 4);
					}

					egAddToCell(child,
								x0 ? addNode.lo[0] >= child->mid[0] : 0, addNode.hi[0] >= child->mid[0],
								addNode.lo[1] >= child->mid[1], y1 ? 1 : addNode.hi[1] >= child->mid[1],
								z0 ? addNode.lo[2] >= child->mid[2] : 0, addNode.hi[2] >= child->mid[2]);
				}
			}
			
			if(y1){
				if(!z0){
					child = children[1][1][0];

					if(!child){
						child = children[1][1][0] = createEntityGridCell(	cellSize / 2,
																		mid[0] + cellSize / 4,
																		mid[1] + cellSize / 4,
																		mid[2] - cellSize / 4);
					}

					egAddToCell(child,
								x0 ? addNode.lo[0] >= child->mid[0] : 0, addNode.hi[0] >= child->mid[0],
								y0 ? addNode.lo[1] >= child->mid[1] : 0, addNode.hi[1] >= child->mid[1],
								addNode.lo[2] >= child->mid[2], z1 ? 1 : addNode.hi[2] >= child->mid[2]);
				}

				if(z1){
					child = children[1][1][1];

					if(!child){
						child = children[1][1][1] = createEntityGridCell(	cellSize / 2,
																		mid[0] + cellSize / 4,
																		mid[1] + cellSize / 4,
																		mid[2] + cellSize / 4);
					}

					egAddToCell(child,
								x0 ? addNode.lo[0] >= child->mid[0] : 0, addNode.hi[0] >= child->mid[0],
								y0 ? addNode.lo[1] >= child->mid[1] : 0, addNode.hi[1] >= child->mid[1],
								z0 ? addNode.lo[2] >= child->mid[2] : 0, addNode.hi[2] >= child->mid[2]);
				}
			}
		}
	}
}

static void egAdd(	EntityGrid* grid,
					int pos[3],
					EntityGridNode* node,
					int radius,
					int radius_hi_bits,
					int use_small_cells)
{
	static int max_entries_count = 0;
	int mid[3];
	int x0, x1;
	int y0, y1;
	int z0, z1;

	addNode.largeCellSize = 1 << radius_hi_bits;
	addNode.smallCellSize = 1 << (radius_hi_bits - use_small_cells);
	
	if(addNode.smallCellSize >= grid->rootCell.cellSize){
		addNode.smallCellSize = grid->rootCell.cellSize >> use_small_cells;
		addNode.largeCellSize = grid->rootCell.cellSize;
	}
	
	addNode.node = node;
	addNode.ent = node->ent;
	addNode.entFlags = node->entFlags;
	addNode.entries = node->entries;
	addNode.entries_count = 0;

	addNode.lo[0] = pos[0] - radius;
	addNode.lo[1] = pos[1] - radius;
	addNode.lo[2] = pos[2] - radius;
	
	addNode.hi[0] = pos[0] + radius;
	addNode.hi[1] = pos[1] + radius;
	addNode.hi[2] = pos[2] + radius;

	copyVec3(grid->rootCell.mid, mid);

	if(addNode.lo[0] < mid[0]){
		x0 = 0;
		x1 = addNode.hi[0] > mid[0];
	}else{
		x0 = 1;
		x1 = 1;
	}
	
	if(addNode.lo[1] < mid[1]){
		y0 = 0;
		y1 = addNode.hi[1] > mid[1];
	}else{
		y0 = 1;
		y1 = 1;
	}

	if(addNode.lo[2] < mid[2]){
		z0 = 0;
		z1 = addNode.hi[2] > mid[2];
	}else{
		z0 = 1;
		z1 = 1;
	}

	egAddToCell(&grid->rootCell, x0, x1, y0, y1, z0, z1);
	
	node->entries_count = addNode.entries_count;
	node->grid = grid;
	grid->nodeCount++;
	grid->nodeEntryCount += addNode.entries_count;
	
	//if(addNode.entries_count > max_entries_count){
	//	max_entries_count = addNode.entries_count;
	//	
	//	printf("new max entries: %d\n", max_entries_count);
	//}
}

void egRemove(EntityGridNode* node){
	int count = node->entries_count;
	EntityGridNodeEntry* curEntry;
	
	if(!count){
		return;
	}

	assert(node->grid->nodeEntryCount >= count);
	assert(node->grid->nodeCount > 0);
	
	node->grid->nodeCount--;
	node->grid->nodeEntryCount -= count;
	
	assert(node->grid->nodeCount || !node->grid->nodeEntryCount);

	for(curEntry = node->entries; count; curEntry++){
		EntityGridNodeEntry nodeEntry = *curEntry;
		EntityGridCellEntryArray* cellHeadArray = nodeEntry.cell->entries;
		int lastIndex = cellHeadArray->count - 1;

		count--;
		
		if(lastIndex < 0 || lastIndex >= ARRAY_SIZE(cellHeadArray->slot)){
			egVerifyAllGrids(node);
			
			assertmsg(0, "Bad grid entry count.");
		}
		
		// Copy the last slot of the head entry array into the newly vacant slot.

		*nodeEntry.slot = cellHeadArray->slot[lastIndex];
		
		// The node entry that was moved into this slot is now made to point to its new slot.
			
		nodeEntry.slot->entry->slot = nodeEntry.slot;
		
		if(lastIndex == 0){
			nodeEntry.cell->entries = cellHeadArray->next;
			
			MP_FREE(EntityGridCellEntryArray, cellHeadArray);
		}else{
			cellHeadArray->count--;
		}
		
		if(nodeEntry.slot->entry->cell != nodeEntry.cell){
			egVerifyAllGrids(node);
			
			assertmsg(0, "Old cell doesn't match new cell.");
		}
	}
	
	node->entries_count = 0;
}

S32 egUseQTrunc;
AUTO_CMD_INT(egUseQTrunc, egUseQTrunc);

S32 entGridIsGridPosDifferent(	const EntGridPosCopy* copy,
								const Vec3 posF32)
{
	IVec3	posS32;
	IVec3	posGrid;
	S32		ret;
	
	PERFINFO_AUTO_START_FUNC();
	
	if(egUseQTrunc){
		qtruncVec3(posF32, posS32);
	}else{
		copyVec3(posF32, posS32);
	}
	
	posGrid[0] = posS32[0] >> (copy->bitsRadius - 1);
	posGrid[1] = posS32[1] >> (copy->bitsRadius - 1);
	posGrid[2] = posS32[2] >> (copy->bitsRadius - 1);
	
	ret = !sameVec3(copy->posGrid, posGrid);

	PERFINFO_AUTO_STOP();
	
	return ret;
}

void egUpdate(	int iPartitionIdx,
				int grid, 
				EntityGridNode* node,
				EntGridPosCopy* gridPosCopy,
				S32* copyUpdatedOut,
				const Vec3 posF32,
				int forceUpdate)
{
	IVec3		posS32;
	IVec3		grid_pos;
	S32			radius_bits;
	S32			use_small_cells;
	EntityGrid*	gridPtr;
	
	if(	node->entries_count &&
		sameVec3((S32*)node->pos, (S32*)posF32) &&
		!forceUpdate)
	{
		return;
	}
	
	copyVec3(posF32, node->pos);

	use_small_cells = node->use_small_cells;
	
	radius_bits = node->radius_hi_bits - use_small_cells;

	if(egUseQTrunc){
		qtruncVec3(posF32, posS32);
	}else{
		copyVec3(posF32, posS32);
	}
	
	grid_pos[0] = posS32[0] >> (radius_bits - 1);
	grid_pos[1] = posS32[1] >> (radius_bits - 1);
	grid_pos[2] = posS32[2] >> (radius_bits - 1);
	
	if(	node->entries_count &&
		sameVec3(grid_pos, node->grid_pos) &&
		!forceUpdate)
	{
		return;
	}

	if(egdn==node)
		printf("Node:%p (%d %d %d) to (%d %d %d)\n", node, vecParamsXYZ(node->grid_pos), vecParamsXYZ(grid_pos));
	
	if(gridPosCopy){
		if(	!sameVec3(gridPosCopy->posGrid, grid_pos) ||
			radius_bits != gridPosCopy->bitsRadius)
		{
			copyVec3(grid_pos, gridPosCopy->posGrid);
			gridPosCopy->bitsRadius = radius_bits;
			
			*copyUpdatedOut = 1;
		}
	}
	
	radius_bits += use_small_cells;
	
	copyVec3(grid_pos, node->grid_pos);

	if((gridPtr=egFromPartition(iPartitionIdx,grid))==NULL)
	{
#ifdef GAMESERVER
		if(partition_ExistsByIdx(iPartitionIdx))
#endif
		{
			EntityGridSet *set = calloc(1,sizeof(EntityGridSet));
			set->iPartitionIdx = iPartitionIdx;
			eaPush(&entityGrids,set);
			egInitGrid(&set->entityGrid[0], 32 * 1024 - 1);
			egInitGrid(&set->entityGrid[1], 32 * 1024 - 1);
			egInitGrid(&set->entityGrid[2], 32 * 1024 - 1);
			egInitGrid(&set->entityGrid[3], 32 * 1024 - 1);
			gridPtr = &set->entityGrid[grid];
		}
#ifdef GAMESERVER
		else
		{
			// Trying to update for a non-existent partition
			// Can happen during entity cleanup and just be ignored
			return;
		}
#endif
	}

	PERFINFO_AUTO_START("egRemove", 1);
		egRemove(node);
	PERFINFO_AUTO_STOP();

	PERFINFO_AUTO_START("egAdd", 1);
		egAdd(gridPtr, posS32, node, node->radius_lo, radius_bits, use_small_cells);
	PERFINFO_AUTO_STOP();
}

int egGetNodesInRange(int iPartitionIdx, int grid, const Vec3 posF32, Entity** nodeArray)
{
	EntityGrid *gridPtr = egFromPartition(iPartitionIdx,grid);
	EntityGridCell* cell = &gridPtr->rootCell;
	int pos[3];
	void** arrayCur = (void**)nodeArray;

	PERFINFO_AUTO_START_FUNC();
	
	pos[0] = posF32[0];
	pos[1] = posF32[1];
	pos[2] = posF32[2];

	while(cell){
		int x, y, z;
		EntityGridCellEntryArray* entry = cell->entries;
		EntityGridCell* halfCell;
		int count;
		EntityGridCellEntryArraySlot* slot;
		
		while(entry){
			count = entry->count;
			slot = entry->slot;

			while(count){
				count--;
				*arrayCur++ = slot->ent;
				slot++;
			}
			
			entry = entry->next;
		}
		
		x = pos[0] < cell->mid[0] ? 0 : 1;	 
		y = pos[1] < cell->mid[1] ? 0 : 1;
		z = pos[2] < cell->mid[2] ? 0 : 1;
		
		// Check the half-cells.
		
		halfCell = cell->children.xy[z];
		
		if(halfCell){
			entry = halfCell->entries;
			
			while(entry){
				count = entry->count;
				slot = entry->slot;

				while(count){
					count--;
					*arrayCur++ = slot->ent;
					slot++;
				}
				
				entry = entry->next;
			}
		}

		halfCell = cell->children.yz[x];
		
		if(halfCell){
			entry = halfCell->entries;
			
			while(entry){
				count = entry->count;
				slot = entry->slot;

				while(count){
					count--;
					*arrayCur++ = slot->ent;
					slot++;
				}
				
				entry = entry->next;
			}
		}

		halfCell = cell->children.xz[y];
		
		if(halfCell){
			entry = halfCell->entries;
			
			while(entry){
				count = entry->count;
				slot = entry->slot;

				while(count){
					count--;
					*arrayCur++ = slot->ent;
					slot++;
				}
				
				entry = entry->next;
			}
		}

		cell = cell->children.xyz[x][y][z];
	}
	PERFINFO_AUTO_STOP();
	return arrayCur - (void**)nodeArray;
}

int egGetNodesInRangeWithDistAndFlags(int iPartitionIdx, int grid, const Vec3 posF32, F32 dist, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, Entity** nodeArray, Entity* sourceEnt)
{
	EntityGrid *gridPtr = egFromPartition(iPartitionIdx,grid);
	EntityGridCell* cell = &gridPtr->rootCell;
	int pos[3];
	void** arrayCur = (void**)nodeArray;
	const S32 useDist = !!dist;

	Vec3 slotEntPos;
	F32 checkDistSQR = 0;

	PERFINFO_AUTO_START_FUNC();

	if(useDist && sourceEnt)
		dist += sourceEnt->collRadiusCached;
	
	pos[0] = posF32[0];
	pos[1] = posF32[1];
	pos[2] = posF32[2];

	while(cell){
		int x, y, z;
		EntityGridCellEntryArray* entry = cell->entries;
		EntityGridCell* halfCell;
		int count;
		EntityGridCellEntryArraySlot* slot;
		
		while(entry){
			count = entry->count;
			slot = entry->slot;

			while(count){
				count--;
				if(	FlagsMatchAll(slot->entFlags,entFlagsToMatch) &&
					FlagsMatchNone(slot->entFlags,entFlagsToExclude))
				{
					if(useDist)
					{
						entGetPos(slot->ent, slotEntPos);
						checkDistSQR = dist + slot->ent->collRadiusCached;
						checkDistSQR = SQR(checkDistSQR);
					}
					if(!useDist || distance3Squared(posF32, slotEntPos) <= checkDistSQR)
						*arrayCur++ = slot->ent;
				}
				slot++;
			}
			
			entry = entry->next;
		}
		
		x = pos[0] < cell->mid[0] ? 0 : 1;	 
		y = pos[1] < cell->mid[1] ? 0 : 1;
		z = pos[2] < cell->mid[2] ? 0 : 1;
		
		// Check the half-cells.
		
		halfCell = cell->children.xy[z];
		
		if(halfCell){
			entry = halfCell->entries;
			
			while(entry){
				count = entry->count;
				slot = entry->slot;

				while(count){
					count--;
					if(	FlagsMatchAll(slot->entFlags,entFlagsToMatch) &&
						FlagsMatchNone(slot->entFlags,entFlagsToExclude))
					{
						if(useDist)
						{
							entGetPos(slot->ent, slotEntPos);
							checkDistSQR = dist + slot->ent->collRadiusCached;
							checkDistSQR = SQR(checkDistSQR);
						}
						if(!useDist || distance3Squared(posF32, slotEntPos) <= checkDistSQR)
							*arrayCur++ = slot->ent;
					}
					slot++;
				}
				
				entry = entry->next;
			}
		}

		halfCell = cell->children.yz[x];
		
		if(halfCell){
			entry = halfCell->entries;
			
			while(entry){
				count = entry->count;
				slot = entry->slot;

				while(count){
					count--;
					if(	FlagsMatchAll(slot->entFlags,entFlagsToMatch) &&
						FlagsMatchNone(slot->entFlags,entFlagsToExclude))
					{
						if(useDist)
						{
							entGetPos(slot->ent, slotEntPos);
							checkDistSQR = dist + slot->ent->collRadiusCached;
							checkDistSQR = SQR(checkDistSQR);
						}
						if(!useDist || distance3Squared(posF32, slotEntPos) <= checkDistSQR)
							*arrayCur++ = slot->ent;
					}
					slot++;
				}
				
				entry = entry->next;
			}
		}

		halfCell = cell->children.xz[y];
		
		if(halfCell){
			entry = halfCell->entries;
			
			while(entry){
				count = entry->count;
				slot = entry->slot;

				while(count){
					count--;
					if(	FlagsMatchAll(slot->entFlags,entFlagsToMatch) &&
						FlagsMatchNone(slot->entFlags,entFlagsToExclude))
					{
						if(useDist)
						{
							entGetPos(slot->ent, slotEntPos);
							checkDistSQR = dist + slot->ent->collRadiusCached;
							checkDistSQR = SQR(checkDistSQR);
						}
						if(!useDist || distance3Squared(posF32, slotEntPos) <= checkDistSQR)
							*arrayCur++ = slot->ent;
					}
					slot++;
				}
				
				entry = entry->next;
			}
		}

		cell = cell->children.xyz[x][y][z];
	}
	PERFINFO_AUTO_STOP();
	return arrayCur - (void**)nodeArray;
}

int egGetNodesInRangeSaveDist(int iPartitionIdx, int grid, const Vec3 posF32, F32 maxDist, EntityFlags entFlagsToMatch, EntityFlags entFlagsToExclude, EntAndDist** nodeArray, int* size, int* maxSize, Entity* sourceEnt)
{
	EntityGrid *gridPtr = egFromPartition(iPartitionIdx,grid);
	EntityGridCell* cell = &gridPtr->rootCell;
	int pos[3];
	Vec3 slotEntPos;
	F32 sourceCheckRadius = maxDist + sourceEnt->collRadiusCached;

	PERFINFO_AUTO_START_FUNC();
	
	*size = 0;

	pos[0] = posF32[0];
	pos[1] = posF32[1];
	pos[2] = posF32[2];

	while(cell){
		int x, y, z;
		EntityGridCellEntryArray* entry = cell->entries;
		EntityGridCell* halfCell;
		int count;
		EntityGridCellEntryArraySlot* slot;
		
		while(entry){
			count = entry->count;
			slot = entry->slot;

			while(count){
				count--;
				if(slot->ent != sourceEnt && FlagsMatchAll(slot->entFlags,entFlagsToMatch) && FlagsMatchNone(slot->entFlags,entFlagsToExclude))
				{
					F32 distSQR;
					F32 checkDist;
					entGetPos(slot->ent, slotEntPos);
					distSQR = distance3Squared(posF32, slotEntPos);
					checkDist = sourceCheckRadius + slot->ent->collRadiusCached;
					if(!maxDist || distSQR <= SQR(checkDist))
					{
						EntAndDist* ead = dynArrayAddStruct(*nodeArray, *size, *maxSize);

						ead->e = slot->ent;
						ead->maxDistSQR = distSQR;
						ead->maxCollRadius = slot->ent->collRadiusCached;
					}
				}
				slot++;
			}
			
			entry = entry->next;
		}
		
		x = pos[0] < cell->mid[0] ? 0 : 1;	 
		y = pos[1] < cell->mid[1] ? 0 : 1;
		z = pos[2] < cell->mid[2] ? 0 : 1;
		
		// Check the half-cells.
		
		halfCell = cell->children.xy[z];
		
		if(halfCell){
			entry = halfCell->entries;
			
			while(entry){
				count = entry->count;
				slot = entry->slot;

				while(count){
					count--;
					if(slot->ent != sourceEnt && FlagsMatchAll(slot->entFlags,entFlagsToMatch) && FlagsMatchNone(slot->entFlags,entFlagsToExclude))
					{
						F32 distSQR;
						F32 checkDist;
						entGetPos(slot->ent, slotEntPos);
						distSQR = distance3Squared(posF32, slotEntPos);
						checkDist = sourceCheckRadius + slot->ent->collRadiusCached;
						if(!maxDist || distSQR <= SQR(checkDist))
						{
							EntAndDist* ead = dynArrayAddStruct(*nodeArray, *size, *maxSize);

							ead->e = slot->ent;
							ead->maxDistSQR = distSQR;
							ead->maxCollRadius = slot->ent->collRadiusCached;
						}
					}
					slot++;
				}
				
				entry = entry->next;
			}
		}

		halfCell = cell->children.yz[x];
		
		if(halfCell){
			entry = halfCell->entries;
			
			while(entry){
				count = entry->count;
				slot = entry->slot;

				while(count){
					count--;
					if(slot->ent != sourceEnt && FlagsMatchAll(slot->entFlags,entFlagsToMatch) && FlagsMatchNone(slot->entFlags,entFlagsToExclude))
					{
						F32 distSQR;
						F32 checkDist;
						entGetPos(slot->ent, slotEntPos);
						distSQR = distance3Squared(posF32, slotEntPos);
						checkDist = sourceCheckRadius + slot->ent->collRadiusCached;
						if(!maxDist || distSQR <= SQR(checkDist))
						{
							EntAndDist* ead = dynArrayAddStruct(*nodeArray, *size, *maxSize);

							ead->e = slot->ent;
							ead->maxDistSQR = distSQR;
							ead->maxCollRadius = slot->ent->collRadiusCached;
						}
					}
					slot++;
				}
				
				entry = entry->next;
			}
		}

		halfCell = cell->children.xz[y];
		
		if(halfCell){
			entry = halfCell->entries;
			
			while(entry){
				count = entry->count;
				slot = entry->slot;

				while(count){
					count--;
					if(slot->ent != sourceEnt && FlagsMatchAll(slot->entFlags,entFlagsToMatch) && FlagsMatchNone(slot->entFlags,entFlagsToExclude))
					{
						F32 distSQR;
						F32 checkDist;
						entGetPos(slot->ent, slotEntPos);
						distSQR = distance3Squared(posF32, slotEntPos);
						checkDist = sourceCheckRadius + slot->ent->collRadiusCached;
						if(!maxDist || distSQR <= SQR(checkDist))
						{
							EntAndDist* ead = dynArrayAddStruct(*nodeArray, *size, *maxSize);

							ead->e = slot->ent;
							ead->maxDistSQR = distSQR;
							ead->maxCollRadius = slot->ent->collRadiusCached;
						}
					}
					slot++;
				}
				
				entry = entry->next;
			}
		}

		cell = cell->children.xyz[x][y][z];
	}
	
	PERFINFO_AUTO_STOP();

	return *size;
}

