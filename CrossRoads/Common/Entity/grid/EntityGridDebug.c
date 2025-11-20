#include "EntityGrid.h"
#include "EntityGridPrivate.h"
#include "EntityIterator.h"

EntityGridNode *egdn = NULL;

static void egVerifyGridCell(EntityGridCell* cell){
	EntityGridCellEntryArray* entries;
	int x, y, z;
	
	if(!cell){
		return;
	}
	
	assert(!(cell->cellSize & (cell->cellSize - 1)));

	for(entries = cell->entries; entries; entries = entries->next){
		int i;
		for(i = 0; i < entries->count; i++){
			assert(entries->slot[i].entry->cell == cell);
			assert(entries->slot[i].entry->slot == entries->slot + i);
		}
	}
	
	for(x = 0; x < 2; x++){
		for(y = 0; y < 2; y++){
			for(z = 0; z < 2; z++){
				egVerifyGridCell(cell->children.xyz[x][y][z]);
			}
		}
	}
	
	for(z = 0; z < 2; z++){
		egVerifyGridCell(cell->children.xy[z]);
	}
	
	for(y = 0; y < 2; y++){
		egVerifyGridCell(cell->children.xz[y]);
	}
	
	for(x = 0; x < 2; x++){
		egVerifyGridCell(cell->children.yz[x]);
	}
}

static void egVerifyNode(EntityGridNode* node){
	int i;
	
	if(!node){
		return;
	}
	
	for(i = 0; i < node->entries_count; i++){
		EntityGridCellEntryArray* entries;
		
		assert(node->entries[i].slot->entry == node->entries + i);
		assert(node->entries[i].slot->ent == node->ent);
		
		for(entries = node->entries[i].cell->entries; entries; entries = entries->next){
			int j;
			
			for(j = 0; j < entries->count; j++){
				if(entries->slot[j].entry == node->entries + i){
					assert(entries->slot + j == node->entries[i].slot);
					break;
				}
			}
			
			if(j != entries->count){
				break;
			}
		}
		
		assert(entries);
	}
}

void egVerifyAllGrids(EntityGridNode* excludeNode){
	int i,j;
	EntityIterator* iter = entGetIteratorAllTypesAllPartitions(0,0); 
	Entity* e;

	for(j = eaSize(&entityGrids)-1; j>=0; j--){
		for(i = 0; i < 4; i++){
			egVerifyGridCell(&entityGrids[j]->entityGrid[i].rootCell);
		}
	}
	
	while(e = EntityIteratorGetNext(iter))
	{
		if(e->egNode != excludeNode)
			egVerifyNode(e->egNode);
		
		if(e->egNodePlayer != excludeNode)
			egVerifyNode(e->egNodePlayer);
	}

	EntityIteratorRelease(iter);
}

AUTO_COMMAND;
void egPrintEntries(Entity *e)
{
	int i;
	printf("Node: r=%.2f, %d, %d", e->egNodePlayer->radius, e->egNodePlayer->radius_lo, e->egNodePlayer->radius_hi_bits);
	for(i=0; i<e->egNodePlayer->entries_count; i++)
	{
		EntityGridNodeEntry *entry = &e->egNodePlayer->entries[i];

		printf("%d %d (%3d, %3d, %3d)\n", i, entry->cell->cellSize, vecParamsXYZ(entry->cell->mid));
	}
}

AUTO_COMMAND;
void egDebugEnt(Entity *e)
{
	if(e->egNodePlayer)
		egdn = e->egNodePlayer;
	else
		egdn = e->egNode;
}

#if ENTITY_GRID_DEBUG


static struct {
	S32 nodeCount;
	S32 nodeEntryCount;
	
	StashTable htFoundNodes;
} verifyData;

void egVerifyGridCellCallback(EntityGridCell* cell, EntityGridVerifyCallback callback){
	EntityGridCellEntryArray* entries;
	S32 i;
	
	if(!cell){
		return;
	}
	
	entries = cell->entries;
	
	for(; entries; entries = entries->next){
		for(i = 0; i < entries->count; i++){
			EntityGridCellEntryArraySlot* slot = entries->slot + i;

			
			if ( stashAddressAddInt(verifyData.htFoundNodes, slot->node, 1, false))
				verifyData.nodeCount++;
		}
		
		verifyData.nodeEntryCount += entries->count;
	}
	
	for(i = 0; i < 2; i++){
		egVerifyGridCellCallback(cell->children.xy[i], callback);
		egVerifyGridCellCallback(cell->children.xz[i], callback);
		egVerifyGridCellCallback(cell->children.yz[i], callback);
	}

	for(i = 0; i < 8; i++){
		egVerifyGridCellCallback(cell->children.xyz[i&1][(i&2)>>1][(i&4)>>2], callback);
	}
}

void egVerifyGrid(int iPartitionIdx, int gridIndex, EntityGridVerifyCallback callback){
	EntityGrid* grid = egFromPartition(iPartitionIdx,gridIndex);
	
	if(!verifyData.htFoundNodes){
		stashTableCreateAddress(1000);
	}
	
	stashTableClear(verifyData.htFoundNodes);
	
	verifyData.nodeCount = 0;
	verifyData.nodeEntryCount = 0;

	egVerifyGridCellCallback(&grid->rootCell, callback);
	
	assert(grid->nodeCount == verifyData.nodeCount);
	assert(grid->nodeEntryCount == verifyData.nodeEntryCount);
}

#endif // ENTITY_GRID_DEBUG

