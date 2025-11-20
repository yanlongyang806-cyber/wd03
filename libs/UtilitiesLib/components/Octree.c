#include <stdio.h>
#include <string.h>
#include "timing.h"

#include "earray.h"
#include "Frustum.h"
#include "Octree.h"
#include "utils.h"
#include "bounds.h"

#define OCTREE_DEBUG_DRAW 0
#define OCTREE_USE_MEMPOOLS 1

#if OCTREE_USE_MEMPOOLS
#include "memorypool.h"
#endif


AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World);); // On the client, currently only used by world stuff

#define OCT_NUMCELLS			2
#define OCT_NUMCELLS_SQUARE		(OCT_NUMCELLS * OCT_NUMCELLS)
#define OCT_NUMCELLS_CUBE		(OCT_NUMCELLS * OCT_NUMCELLS * OCT_NUMCELLS)
#define OCT_BIGGEST_BRICK		32768

typedef struct OctreeEntries
{
	OctreeEntryPtr			entries[7];
	struct OctreeEntries	*next;
} OctreeEntries;

typedef struct OctreeIdxList
{
	OctreeEntryPtr			*entries[7];
	struct OctreeIdxList	*next;
} OctreeIdxList;

typedef struct OctreeCellCE
{
	OctreeVisBounds			bounds;
	U8						inherited_bits;
	Vec3					center;
	F32						size; // half width
	struct OctreeCellCE		**children;
	OctreeEntries			*entries;
} OctreeCellCE;

// This typedef helps fix a debug symbol conflict with the 3rd-party physics code.
typedef OctreeCellCE OctreeCell;

typedef struct OctreeSearchCell
{
	OctreeCell *cell;
	int no_frustum_check;
	Vec3 center;
} OctreeSearchCell;

typedef struct Octree
{
	OctreeCell				*cell;
	U32						tag;
	U32						valid_id;
	OctreeCell				**search_cell_earray;
	OctreeSearchCell		*search_cell_dynarray;
	int						search_cell_count, search_cell_max;
	bool					need_vis_recompute;
#if OCTREE_USE_MEMPOOLS
	int						mempool_size;
	MemoryPool				entries_mempool;
	MemoryPool				childptrs_mempool;
	MemoryPool				idxlist_mempool;
	MemoryPool				cells_mempool;
#else
	MEM_DBG_STRUCT_PARMS
#endif
} Octree;

typedef struct
{
	Octree					*octree;
	F32						fit_size;
	Vec3					min;
	Vec3					size;
	OctreeEntryPtr			entry;
} OctreeInsertState;


#if OCTREE_USE_MEMPOOLS
	MP_DEFINE(Octree);
#endif

__forceinline static OctreeEntries *octreeAllocEntries(Octree *octree)
{
#if OCTREE_USE_MEMPOOLS
	return mpAlloc(octree->entries_mempool);
#else
	return stcalloc(1, sizeof(OctreeEntries), octree);
#endif
}

__forceinline static OctreeCell **octreeAllocChildPtrs(Octree *octree)
{
#if OCTREE_USE_MEMPOOLS
	return mpAlloc(octree->childptrs_mempool);
#else
	return stcalloc(OCT_NUMCELLS_CUBE, sizeof(OctreeCell *), octree);
#endif
}

__forceinline static OctreeCell *octreeAllocCell(Octree *octree, F32 cell_size)
{
#if OCTREE_USE_MEMPOOLS
	OctreeCell *cell = mpAlloc(octree->cells_mempool);
#else
	OctreeCell *cell = stcalloc(1, sizeof(OctreeCell), octree);
#endif
	cell->size = cell_size;
	return cell;
}

__forceinline static OctreeIdxList *octreeAllocIdxList(Octree *octree)
{
#if OCTREE_USE_MEMPOOLS
	return mpAlloc(octree->idxlist_mempool);
#else
	return stcalloc(1, sizeof(OctreeIdxList), octree);
#endif
}

__forceinline static void octreeFreeEntries(Octree *octree, OctreeEntries *ents)
{
#if OCTREE_USE_MEMPOOLS
	mpFree(octree->entries_mempool,ents);
#else
	free(ents);
#endif
}

__forceinline static void octreeFreeChildPtrs(Octree *octree, OctreeCell **child_ptrs)
{
#if OCTREE_USE_MEMPOOLS
	mpFree(octree->childptrs_mempool,child_ptrs);
#else
	free(child_ptrs);
#endif
}

__forceinline static void octreeFreeCellInternal(Octree *octree, OctreeCell *cell)
{
#if OCTREE_USE_MEMPOOLS
	mpFree(octree->cells_mempool,cell);
#else
	free(cell);
#endif
}

__forceinline static void octreeFreeIdxList(Octree *octree, OctreeIdxList *idxlist)
{
#if OCTREE_USE_MEMPOOLS
	mpFree(octree->idxlist_mempool,idxlist);
#else
	free(idxlist);
#endif
}

static void freeEntries(Octree *octree,OctreeEntries *ents)
{
	OctreeEntries *next;
	int i;

	for(;ents;ents=next)
	{
		for(i=0;i<ARRAY_SIZE(ents->entries);i++)
		{
			if (ents->entries[i])
			{
				ents->entries[i]->idx_list = NULL;
				ents->entries[i]->octree = NULL;
			}
		}

		next = ents->next;
		octreeFreeEntries(octree,ents);
	}
}

static OctreeEntryPtr *findEntry(OctreeEntries *ents)
{
	int		i;

	for(;ents;ents = ents->next)
	{
		for(i=0;i<ARRAY_SIZE(ents->entries);i++)
		{
			if (!ents->entries[i])
				return &ents->entries[i];
		}
	}
	return 0;
}

static OctreeEntryPtr **findListIdx(OctreeIdxList *list)
{
	int		i;

	for(;list;list = list->next)
	{
		for(i=0;i<ARRAY_SIZE(list->entries);i++)
		{
			if (!list->entries[i])
				return &list->entries[i];
		}
	}
	return 0;
}

static void octreeFreeCell(Octree *octree,OctreeCell *cell)
{
	if (!cell)
		return;
	if (cell->children)
	{
		int i;
		for (i = 0; i < OCT_NUMCELLS_CUBE; ++i)
			octreeFreeCell(octree,cell->children[i]);
		octreeFreeChildPtrs(octree, cell->children);
	}
	freeEntries(octree,cell->entries);
	octreeFreeCellInternal(octree, cell);
}

Octree *octreeCreateDebug(const Vec3 center, F32 size, int mempool_size MEM_DBG_PARMS)
{
	Octree *octree;

	if (!center)
		center = zerovec3;
	
	if (size <= 0)
		size = OCT_BIGGEST_BRICK;

#if OCTREE_USE_MEMPOOLS

	MP_CREATE(Octree, 16);
	octree = MP_ALLOC(Octree);

	if (mempool_size <= 0)
		mempool_size = 64;

	octree->mempool_size = mempool_size;

	octree->entries_mempool = createMemoryPoolNamed("OctreeEntries", caller_fname, line);
	initMemoryPool(octree->entries_mempool,sizeof(OctreeEntries),octree->mempool_size);

	octree->childptrs_mempool = createMemoryPoolNamed("OctreeChildPtrs", caller_fname, line);
	initMemoryPool(octree->childptrs_mempool,OCT_NUMCELLS_CUBE * sizeof(OctreeCell *),octree->mempool_size/2);

	octree->cells_mempool = createMemoryPoolNamed("OctreeCell", caller_fname, line);
	initMemoryPool(octree->cells_mempool,sizeof(OctreeCell),octree->mempool_size);

	octree->idxlist_mempool = createMemoryPoolNamed("OctreeIdxList", caller_fname, line);
	initMemoryPool(octree->idxlist_mempool,sizeof(OctreeIdxList),octree->mempool_size);

#else
	
	octree = scalloc(1, sizeof(Octree));

	MEM_DBG_STRUCT_PARMS_INIT(octree);

#endif

	octree->cell = octreeAllocCell(octree, size);
	copyVec3(center, octree->cell->center);

	return octree;
}

void octreeDestroy(Octree *octree)
{
	if (!octree) {
		return;
	}
	octreeRemoveAll(octree);
	eaDestroy(&octree->search_cell_earray);
	SAFE_FREE(octree->search_cell_dynarray);
#if OCTREE_USE_MEMPOOLS
	MP_FREE(Octree, octree);
#else
	free(octree);
#endif
}

void octreeRemove(OctreeEntry *entry)
{
	OctreeIdxList *list, *next;
	Octree *octree = entry->octree;
	int i;

	if (!octree)
		return;

	for (list = entry->idx_list; list; list = next)
	{
		for(i=0;i<ARRAY_SIZE(list->entries);i++)
		{
			if (list->entries[i])
				*list->entries[i] = 0;
		}
		next = list->next;
		octreeFreeIdxList(octree,list);
	}

	entry->idx_list = NULL;
	entry->octree = NULL;
	octree->need_vis_recompute = true;
}

__forceinline static void mergeBounds(OctreeVisBounds *dst, const OctreeVisBounds *src)
{
	F32 radius, dvlen;
	int j;
	Vec3 dv;

	// update bounding box
	for (j=0;j<3;j++)
	{
		// Check if the bounding box is smaller than the diameter.
		if (src->max[j] - src->min[j] < src->radius * 2)
			break;
	}
	if (j == 3)
	{
		// The bounding box is LARGER than the diameter in all dimensions, so use the radius.
		for (j=0;j<3;j++)
		{
			MAX1(dst->max[j], src->mid[j] + src->radius);
			MIN1(dst->min[j], src->mid[j] - src->radius);
		}
	}
	else
	{
		for (j=0;j<3;j++)
		{
			MAX1(dst->max[j],src->max[j]);
			MIN1(dst->min[j],src->min[j]);
		}
	}

	// calc mid and radius
	subVec3(dst->max, dst->min, dst->mid);
	radius = lengthVec3(dst->mid) * 0.5f;
	scaleVec3(dst->mid, 0.5f, dst->mid);
	addVec3(dst->mid, dst->min, dst->mid);

	subVec3(src->mid, dst->mid, dv);
	dvlen = lengthVec3(dv);
	MAX1(radius, dvlen + src->radius);
	MAX1(dst->radius, radius);

	MAX1(dst->vis_dist, dvlen + src->vis_dist);
}

static int cellRecomputeBounds(Octree *octree, OctreeCell *cell, bool recompute_children)
{
	OctreeEntries *ents;
	int i, bounds_count = 0, entry_count = 0;

	// loop through the children of this cell
	if (cell->children)
	{
		for (i = 0; i < OCT_NUMCELLS_CUBE; ++i)
		{
			OctreeCell *child = cell->children[i];
			if (child)
			{
				if (recompute_children)
				{
					if (!cellRecomputeBounds(octree, child, true))
					{
						// child is empty, free it
						octreeFreeCell(octree, child);
						cell->children[i] = NULL;
						continue;
					}
				}

				if (!bounds_count)
				{
					CopyStructs(&cell->bounds, &child->bounds, 1);
				}
				else
				{
					mergeBounds(&cell->bounds, &child->bounds);
				}

				++bounds_count;
			}
		}
	}

	// loop through the entries in this cell
	for (ents = cell->entries; ents; ents = ents->next)
	{
		for (i = 0; i < ARRAY_SIZE(ents->entries); ++i)
		{
			OctreeEntryPtr entry = ents->entries[i];
			if (entry)
			{
				if (!bounds_count)
				{
					CopyStructs(&cell->bounds, &entry->bounds, 1);
				}
				else
				{
					mergeBounds(&cell->bounds, &entry->bounds);
				}

				++bounds_count;
				++entry_count;
			}
		}
	}

	if (cell->entries && !entry_count)
	{
		// the entry list has no entries in it, free it
		freeEntries(octree, cell->entries);
		cell->entries = NULL;
	}

	// clamp vis bounds to cell bounds
	// leave radius and mid untouched because vis_dist relies on them
	MAX1(cell->bounds.min[0], cell->center[0] - cell->size);
	MAX1(cell->bounds.min[1], cell->center[1] - cell->size);
	MAX1(cell->bounds.min[2], cell->center[2] - cell->size);

	MIN1(cell->bounds.max[0], cell->center[0] + cell->size);
	MIN1(cell->bounds.max[1], cell->center[1] + cell->size);
	MIN1(cell->bounds.max[2], cell->center[2] + cell->size);

	return bounds_count;
}

static void octreeInsert(OctreeInsertState *state,OctreeCell *cell)
{
	int			i,x,y,z,filled=1;
	OctreeCell	**child_ptr;
	int			idxa[3],idxb[3];
	F32			tf;

	for(i=0;i<3;i++)
	{
		tf = state->min[i] - cell->center[i];
		idxa[i] = 0;
		if (tf >= 0)
			idxa[i] = 1;

		idxb[i] = 0;
		if (tf + state->size[i] >= 0)
			idxb[i] = 1;

		if (tf > -cell->size || tf + state->size[i] < cell->size)
			filled = 0;
	}

	if (filled || cell->size <= state->fit_size)
	{
		OctreeEntryPtr		*mptr,**mmptr;

		mptr = findEntry(cell->entries);
		if (!mptr)
		{
			OctreeEntries	*ents = octreeAllocEntries(state->octree);

			ents->next = cell->entries;
			cell->entries = ents;
			mptr = findEntry(cell->entries);
		}
		*mptr = state->entry;
		if (state->entry->idx_list)
		{
			mmptr = findListIdx(state->entry->idx_list);
			if (!mmptr)
			{
				OctreeIdxList	*list = octreeAllocIdxList(state->octree);

				list->next = state->entry->idx_list;
				state->entry->idx_list = list;
				mmptr = findListIdx(state->entry->idx_list);
			}
			*mmptr = mptr;
		}
	}
	else
	{
		if (!cell->children)
			cell->children = octreeAllocChildPtrs(state->octree);

		for(y=idxa[1];y<=idxb[1];y++)
		{
			for(z=idxa[2];z<=idxb[2];z++)
			{
				for(x=idxa[0];x<=idxb[0];x++)
				{
					child_ptr = &cell->children[OCT_NUMCELLS_SQUARE * y + OCT_NUMCELLS * z + x];
					if (!*child_ptr)
					{
						*child_ptr = octreeAllocCell(state->octree, cell->size * 0.5f);
						(*child_ptr)->center[0] = cell->center[0] + (x ? (*child_ptr)->size : -(*child_ptr)->size);
						(*child_ptr)->center[1] = cell->center[1] + (y ? (*child_ptr)->size : -(*child_ptr)->size);
						(*child_ptr)->center[2] = cell->center[2] + (z ? (*child_ptr)->size : -(*child_ptr)->size);
					}
					octreeInsert(state,*child_ptr);
				}
			}
		}
	}

	cellRecomputeBounds(state->octree, cell, false);
}


void octreeAddEntry(Octree *octree, OctreeEntry *entry, OctreeGranularity granularity)
{
	Vec3			size;
	int				i;
	F32				fit_size;
	OctreeInsertState state;

	assert(!entry->octree);
	assert(!entry->idx_list);

	subVec3(entry->bounds.max,entry->bounds.min,size);
	for(i=0;i<3;i++)
		if (size[i] < 0.f)
			size[i] = 0.f;

	fit_size = MAX(size[0],size[1]);
	fit_size = MAX(fit_size,size[2]);
	fit_size *= 0.5f;

	switch (granularity)
	{
		xcase OCT_FINE_GRANULARITY:
			fit_size = MAX(fit_size,4.f); // smallest block: 4
		xcase OCT_MEDIUM_GRANULARITY:
			fit_size = MAX(fit_size,16.f); // smallest block: 16
		xcase OCT_ROUGH_GRANULARITY:
			fit_size = MAX(fit_size,64.f); // smallest block: 64
		xcase OCT_VERY_ROUGH_GRANULARITY:
			fit_size = MAX(fit_size,256.f); // smallest block: 512
	}

	state.octree = octree;
	state.fit_size = fit_size;
	state.entry = entry;
	entry->idx_list = octreeAllocIdxList(octree);
	entry->octree = octree;
	entry->last_visited_tag = 0;

	copyVec3(entry->bounds.min, state.min);
	copyVec3(size, state.size);

	octreeInsert(&state,octree->cell);
}

void octreeRemoveAll(Octree *octree)
{
	U32 t;

	if (!octree)
		return;

	octreeFreeCell(octree,octree->cell);

#if OCTREE_USE_MEMPOOLS
	if (octree->entries_mempool)
		destroyMemoryPool(octree->entries_mempool);
	if (octree->childptrs_mempool)
		destroyMemoryPool(octree->childptrs_mempool);
	if (octree->cells_mempool)
		destroyMemoryPool(octree->cells_mempool);
	if (octree->idxlist_mempool)
		destroyMemoryPool(octree->idxlist_mempool);
#endif

	t = octree->valid_id;
	ZeroStruct(octree);
	octree->valid_id = t+1;
}

//////////////////////////////////////////////////////////////////////////
// Find

#define XLO 0x55
#define YLO 0x0f
#define ZLO 0x33

typedef struct OctreeFindState
{
	const Frustum *frustum;
	Vec3	point;
	F32		radius;
	U32		tag;
	Vec3	cam_pos;
	F32		lod_scale;
	bool	use_visdist, use_box;
	Vec3	box_min, box_max;
	Mat4	box_trans, box_trans_inv;
	OctreeFindCallback	callback;
	OctreeSphereVisCallback sphere_vis_callback;
	OctreeOcclusionCallback occlusion_callback;
	void	*user_data;
	void	***nodes;
	Octree	*octree;

	int				octreeDrawCount;
	int				octreeDrawMax;
	OctreeDrawEntry *octreeDrawData;
	MEM_DBG_STRUCT_PARMS
} OctreeFindState;

__forceinline static bool checkBoundsNotVisible(const OctreeFindState *state,OctreeVisBounds *bounds,int clipped,bool is_leaf,U8 *inherited_bits)
{
	if (state->occlusion_callback)
	{
		int nearClipped;
		Vec4_aligned eye_bounds[8];

		mulBounds(bounds->min, bounds->max, state->frustum->viewmat, eye_bounds);

		nearClipped = frustumCheckBoxNearClipped(state->frustum, eye_bounds);
		if (nearClipped == 2)
		{
			return true;
		}
		// only test non-leaf nodes if they are not clipped by the near plane
		else if ((is_leaf || !nearClipped) && !state->occlusion_callback(state->user_data, eye_bounds, 1, &bounds->occlusion_data.last_update_time, &bounds->occlusion_data.occluded_bits, inherited_bits, NULL, NULL))
		{
			return true;
		}

		return false;
	}

	return clipped != FRUSTUM_CLIP_NONE && !frustumCheckBoxWorld(state->frustum, clipped, bounds->min, bounds->max, NULL, false);
}

__forceinline static void processOctreeEntries(const Octree *octree,const OctreeCell *cell,const OctreeFindState *state,int no_frustum_check)
{
	int i;
	OctreeEntryPtr entry;
	OctreeEntries *ents;

	for(ents=cell->entries;ents;ents=ents->next)
	{
		for(i=0;i<ARRAY_SIZE(ents->entries);i++)
		{
			U8 inherited_bits = cell->inherited_bits;

			entry = ents->entries[i];
			if (!entry)
				continue;

			if (entry->last_visited_tag == state->tag)
				continue;

			entry->last_visited_tag = state->tag;

			if (state->sphere_vis_callback && !state->sphere_vis_callback(entry->node, entry->node_type, state->point, state->radius, state->user_data))
				continue;
			
			if (state->frustum)
			{
				// vis_dist check
				F32 dist_sqr = distance3Squared(entry->bounds.mid, state->cam_pos);
				F32 vis_dist_sqr = state->lod_scale * entry->bounds.vis_dist + entry->bounds.radius;
				vis_dist_sqr = SQR(vis_dist_sqr);
				if (state->use_visdist && dist_sqr > vis_dist_sqr)
					continue;

//				if (!no_frustum_check)
				{
					int clipped;
					if (!(clipped = frustumCheckSphereWorld(state->frustum, entry->bounds.mid, entry->bounds.radius)))
						continue;
					if (checkBoundsNotVisible(state, &entry->bounds, clipped, true, &inherited_bits))
						continue;
				}
			}
			else if (state->use_box)
			{
				if (state->box_trans)
				{
					if (!orientBoxBoxCollision(state->box_min, state->box_max, state->box_trans, entry->bounds.min, entry->bounds.max, unitmat))
						continue;
				}
				else
				{
					if (!boxBoxCollision(state->box_min, state->box_max, entry->bounds.min, entry->bounds.max))
						continue;
				}
			}
			else
			{
				if (!sphereSphereCollision(entry->bounds.mid, entry->bounds.radius, state->point, state->radius))
					continue;
				if (!boxSphereCollision(entry->bounds.min, entry->bounds.max, state->point, state->radius))
					continue;
			}

			if (state->callback)
				state->callback(&entry->bounds, entry->node, entry->node_type, entry->node_meta_data, inherited_bits);
			else
				steaPush(state->nodes, entry->node, state);
		}
	}
}


__forceinline static void processOctreeEntriesDA(const Octree *octree,const OctreeCell *cell,OctreeFindState *state,int no_frustum_check)
{
	int i;
	OctreeEntryPtr entry;
	OctreeEntries *ents;

	const Frustum *state_frustum = state->frustum;
	OctreeFindCallback state_callback = state->callback;
	OctreeSphereVisCallback state_sphere_vis_callback = state->sphere_vis_callback;
	Vec3 state_cam_pos;
	F32 state_lod_scale;
	bool state_use_visdist;

	copyVec3(state->cam_pos, state_cam_pos);
	state_lod_scale = state->lod_scale;
	state_use_visdist = state->use_visdist;

	for(ents=cell->entries;ents;ents=ents->next)
	{
		for(i=0;i<ARRAY_SIZE(ents->entries);i++)
		{
			U8 inherited_bits;

			entry = ents->entries[i];
			if (!entry || entry->last_visited_tag == state->tag)
				continue;

			entry->last_visited_tag = state->tag;

			if (state_sphere_vis_callback && !state_sphere_vis_callback(entry->node, entry->node_type, state->point, state->radius,state->user_data))
				continue;
			
			inherited_bits = cell->inherited_bits;
			if (state_frustum)
			{
				// vis_dist check
				F32 dist_sqr = distance3Squared(entry->bounds.mid, state_cam_pos);
				F32 vis_dist_sqr = state_lod_scale * entry->bounds.vis_dist + entry->bounds.radius;
				vis_dist_sqr = SQR(vis_dist_sqr);
				if (state_use_visdist && dist_sqr > vis_dist_sqr)
					continue;

//				if (!no_frustum_check||force_check)
				{
					int clipped;
					if (!(clipped = frustumCheckSphereWorld(state_frustum, entry->bounds.mid, entry->bounds.radius)))
						continue;
					if (checkBoundsNotVisible(state, &entry->bounds, clipped, true, &inherited_bits))
						continue;
				}
			}
			else
			{
				if (!sphereSphereCollision(entry->bounds.mid, entry->bounds.radius, state->point, state->radius))
					continue;
				if (!boxSphereCollision(entry->bounds.min, entry->bounds.max, state->point, state->radius))
					continue;
			}

			{
				OctreeDrawEntry *newEntry = dynArrayAddStruct_no_memset(state->octreeDrawData, state->octreeDrawCount, state->octreeDrawMax);

				newEntry->entry = entry;
				newEntry->occlusion_inherited_bits = inherited_bits;
			}
		}
	}
}

static void octreeSplitPlanes(Octree *octree,OctreeCell *parent_cell,OctreeFindState *state)
{
	static U8 masks[3] = {XLO,YLO,ZLO};
	F32 r,*point;
	int j;

	PERFINFO_AUTO_START_FUNC();

	parent_cell->inherited_bits = 0;

	eaSetSize(&octree->search_cell_earray, 0);
	eaPush(&octree->search_cell_earray, parent_cell);

	point = state->point;
	r = state->radius;

	for (j = 0; j < eaSize(&octree->search_cell_earray); ++j)
	{
		OctreeCell *cell = octree->search_cell_earray[j];
		int i;
		U8 mask, bit;
		F32 t, *ppos;

		ppos = cell->center;
		t = r + cell->size;

#define CHECK_IN_CELL_AXIS(axis)			\
		if (point[axis] < ppos[axis] - t)	\
			continue;						\
		if (point[axis] > ppos[axis] + t)	\
			continue;

		CHECK_IN_CELL_AXIS(0);
		CHECK_IN_CELL_AXIS(1);
		CHECK_IN_CELL_AXIS(2);

		if (cell->entries)
			processOctreeEntries(octree, cell, state, true);

		if (!cell->children)
			continue;

		mask = 0xff;

#define FILL_MASK_AXIS(axis)				\
		t = point[axis] - ppos[axis];		\
		if (!(t <= r))						\
			mask &= ~masks[axis];			\
		if (!(t >= -r))						\
			mask &= masks[axis];

		FILL_MASK_AXIS(0);
		FILL_MASK_AXIS(1);
		FILL_MASK_AXIS(2);

		if (!mask)
			continue;

		bit = 1;
		for (i = 0; i < OCT_NUMCELLS_CUBE; ++i, bit <<= 1)
		{
			if ((bit & mask) && cell->children[i])
				eaPush(&octree->search_cell_earray, cell->children[i]);
		}
	}

	PERFINFO_AUTO_STOP();
}

void octreeFindInSphereCB(Octree *octree, OctreeFindCallback callback, const Vec3 point, F32 radius, OctreeSphereVisCallback vis_callback, void *user_data)
{
	OctreeFindState	state = {0};

	copyVec3(point, state.point);
	state.radius = radius;
	state.callback = callback;
	state.sphere_vis_callback = vis_callback;
	state.user_data = user_data;
	state.tag = ++octree->tag;
	state.octree = octree;
	octreeSplitPlanes(octree, octree->cell, &state);
}

void octreeFindInSphereEA_dbg(Octree *octree, void ***node_array, const Vec3 point, F32 radius, OctreeSphereVisCallback vis_callback, void *user_data MEM_DBG_PARMS)
{
	OctreeFindState	state = {0};

	copyVec3(point, state.point);
	state.radius = radius;
	state.sphere_vis_callback = vis_callback;
	state.user_data = user_data;
	state.nodes = node_array;
	state.tag = ++octree->tag;
	state.octree = octree;
	MEM_DBG_STRUCT_PARMS_INIT(&state);
	octreeSplitPlanes(octree, octree->cell, &state);
}

void **octreeFindInSphere_dbg(Octree *octree, const Vec3 point, F32 radius, OctreeSphereVisCallback vis_callback, void *user_data MEM_DBG_PARMS)
{
	OctreeFindState	state = {0};
	void **nodes = NULL;

	copyVec3(point, state.point);
	state.radius = radius;
	state.sphere_vis_callback = vis_callback;
	state.user_data = user_data;
	state.nodes = &nodes;
	state.tag = ++octree->tag;
	state.octree = octree;
	MEM_DBG_STRUCT_PARMS_INIT(&state);
	octreeSplitPlanes(octree, octree->cell, &state);
	return nodes;
}

static void octreeSplitPlanesFrustum(Octree *octree,OctreeFindState *state,OctreeCell *parent_cell,Vec3 parent_center,int parent_no_frustum_check)
{
	const Frustum *frustum = state->frustum;
	OctreeSearchCell *search_cell;
	int j, clipped=0;

	PERFINFO_AUTO_START_FUNC();

	parent_cell->inherited_bits = 0;

	octree->search_cell_count = 0;
	search_cell = dynArrayAddStruct_no_memset(octree->search_cell_dynarray, octree->search_cell_count, octree->search_cell_max);

	search_cell->cell = parent_cell;
	copyVec3(parent_center, search_cell->center);
	search_cell->no_frustum_check = parent_no_frustum_check;

	for (j = 0; j < octree->search_cell_count; ++j)
	{
		int no_frustum_check = octree->search_cell_dynarray[j].no_frustum_check;
		OctreeCell *cell = octree->search_cell_dynarray[j].cell;
		Vec3 center;
		F32 child_size, child_radius;
		int i;

		if (cell->entries)
  			processOctreeEntries(octree, cell, state, no_frustum_check);

		if (!cell->children)
			continue;

		child_size = cell->size * 0.5f;
		child_radius = child_size * 1.73205f; // = sqrtf(SQR(child_size) + SQR(child_size) + SQR(child_size))
		copyVec3(octree->search_cell_dynarray[j].center, center);

		for (i = 0; i < OCT_NUMCELLS_CUBE; ++i)
		{
			OctreeCell *child_cell = cell->children[i];
			if (child_cell)
			{
				bool visible = true;

				// vis_dist check
				F32 dist_sqr = distance3Squared(child_cell->bounds.mid, state->cam_pos);
				F32 vis_dist_sqr = state->lod_scale * child_cell->bounds.vis_dist + child_cell->bounds.radius;
				vis_dist_sqr = SQR(vis_dist_sqr);
				if (state->use_visdist && dist_sqr > vis_dist_sqr)
					continue;

				search_cell = dynArrayAddStruct_no_memset(octree->search_cell_dynarray, octree->search_cell_count, octree->search_cell_max);
				search_cell->cell = cell->children[i];

				if (i & 1)
					scaleAddVec3(frustum->viewmat[0], child_size, center, search_cell->center);
				else
					scaleAddVec3(frustum->viewmat[0], -child_size, center, search_cell->center);

				if (i & 4)
					scaleAddVec3(frustum->viewmat[1], child_size, search_cell->center, search_cell->center);
				else
					scaleAddVec3(frustum->viewmat[1], -child_size, search_cell->center, search_cell->center);

				if (i & 2)
					scaleAddVec3(frustum->viewmat[2], child_size, search_cell->center, search_cell->center);
				else
					scaleAddVec3(frustum->viewmat[2], -child_size, search_cell->center, search_cell->center);

				child_cell->inherited_bits = cell->inherited_bits;

				if (no_frustum_check)
				{
					search_cell->no_frustum_check = true;
				}
				else if (clipped = frustumCheckSphere(frustum, search_cell->center, child_radius))
				{
					if (clipped == FRUSTUM_CLIP_NONE)
					{
						search_cell->no_frustum_check = true;
					}
					else if (checkBoundsNotVisible(state, &child_cell->bounds, clipped, false, &child_cell->inherited_bits))
					{
						// not in list, not needed: search_cell->no_frustum_check = false;
						visible = false;
					}
					else
					{
						search_cell->no_frustum_check = false;
					}
				}
				else
				{
					// not in list, not needed: search_cell->no_frustum_check = false;
					visible = false;
				}

				if (!visible)
					--octree->search_cell_count; // undo the dynArrayAddStruct
			}
		}
	}

	PERFINFO_AUTO_STOP();
}

static void octreeSplitPlanesBox(Octree *octree,OctreeFindState *state,OctreeCell *parent_cell)
{
	static U8 masks[3] = {XLO,YLO,ZLO};
	int j;

	PERFINFO_AUTO_START_FUNC();

	parent_cell->inherited_bits = 0;

	eaSetSize(&octree->search_cell_earray, 0);
	eaPush(&octree->search_cell_earray, parent_cell);

	for (j = 0; j < eaSize(&octree->search_cell_earray); ++j)
	{
		OctreeCell *cell = octree->search_cell_earray[j];
		int i;

		if (state->box_trans)
		{
			if (!orientBoxBoxCollision(state->box_min, state->box_max, state->box_trans, cell->bounds.min, cell->bounds.max, unitmat))
				continue;
		}
		else
		{
			if (!boxBoxCollision(state->box_min, state->box_max, cell->bounds.min, cell->bounds.max))
				continue;
		}

		if (cell->entries)
			processOctreeEntries(octree, cell, state, true);

		if (!cell->children)
			continue;

		for (i = 0; i < OCT_NUMCELLS_CUBE; ++i)
		{
			if (cell->children[i])
				eaPush(&octree->search_cell_earray, cell->children[i]);
		}
	}

	PERFINFO_AUTO_STOP();
}


typedef struct WorldStats
{
	int octree_search_cell_max;
	int octree_cells;
	int octree_cells_visradius_culled;
	int octree_cells_tested;
	int octree_cells_visible;
	int octree_cells_trivial_accept;
	int octree_cells_culled;
} WorldStats;

WorldStats debug_world_stats;

void WorldStatReset()
{
	memset(&debug_world_stats, 0, sizeof(debug_world_stats));
}

#if PROFILE_PERF
#define WORLD_STAT(M_sStat) \
	++debug_world_stats.M_sStat;
#define WORLD_STAT_I(M_sStat, M_sIncrement) \
	debug_world_stats.M_sStat += (M_sIncrement);
#else
#define WORLD_STAT(M_sStat) 
#define WORLD_STAT_I(M_sStat, M_sIncrement) 
#endif

#if OCTREE_DEBUG_DRAW
static int debug_drawOctree = 0;

void octreeEnableDraw(int bDraw)
{
	debug_drawOctree = bDraw;
}
#endif

static void octreeSplitPlanesFrustum_GetNonCulledList(Octree *octree,OctreeFindState *state,OctreeCell *parent_cell,Vec3 parent_center)
{
	const Frustum *frustum = state->frustum;
	OctreeSearchCell *search_cell;
	int j, clipped=0;
	F32 child_size, child_radius;
	int i;

	OctreeSearchCell *search_cell_dynarray = octree->search_cell_dynarray;
	int search_cell_count = 0;
	int search_cell_max = octree->search_cell_max;

	Vec3 state_cam_pos;
	F32 state_lod_scale;
	bool state_use_visdist;

	copyVec3(state->cam_pos, state_cam_pos);
	state_lod_scale = state->lod_scale;
	state_use_visdist = state->use_visdist;

	parent_cell->inherited_bits = 0;
	search_cell = dynArrayAddStruct_no_memset(search_cell_dynarray, search_cell_count, search_cell_max);

	search_cell->cell = parent_cell;
	search_cell->no_frustum_check = false;
	copyVec3(parent_center, search_cell->center);

	for (j = 0; j < search_cell_count; ++j)
	{
		const OctreeCell *cell;
		int no_frustum_check;

		search_cell = search_cell_dynarray + j;
		no_frustum_check = search_cell->no_frustum_check;

		if (no_frustum_check)
			continue;
		cell = search_cell->cell;
		if (!cell->children)
			continue;

		child_size = cell->size * 0.5f;
		child_radius = child_size * 1.73205f; // = sqrtf(SQR(child_size) + SQR(child_size) + SQR(child_size))

		for (i = 0; i < OCT_NUMCELLS_CUBE; ++i)
		{
			OctreeCell *child_cell = cell->children[i];
			if (child_cell)
			{
				// vis_dist check
				F32 dist_sqr = distance3Squared(child_cell->bounds.mid, state_cam_pos);
				F32 vis_dist_sqr = child_cell->bounds.radius;
				vis_dist_sqr += state_lod_scale * child_cell->bounds.vis_dist;
				vis_dist_sqr = SQR(vis_dist_sqr);

				WORLD_STAT(octree_cells);

				if (state_use_visdist && dist_sqr > vis_dist_sqr)
				{
					WORLD_STAT(octree_cells_visradius_culled);
					continue;
				}

				search_cell = dynArrayAddStruct_no_memset(search_cell_dynarray, search_cell_count, search_cell_max);
				search_cell->cell = child_cell;
				search_cell->no_frustum_check = no_frustum_check;

				child_cell->inherited_bits = cell->inherited_bits;

				{
					Vec3 center;
					F32 offset;
					offset = child_size;
					if (!(i & 1))
						offset = -offset;
					scaleAddVec3(frustum->viewmat[0], offset, search_cell_dynarray[j].center, center);

					offset = child_size;
					if (!(i & 4))
						offset = -offset;
					scaleAddVec3(frustum->viewmat[1], offset, center, center);

					offset = child_size;
					if (!(i & 2))
						offset = -offset;
					scaleAddVec3(frustum->viewmat[2], offset, center, center);
					copyVec3(center, search_cell->center);
				}


				WORLD_STAT(octree_cells_tested);
				if (clipped = frustumCheckSphere(frustum, search_cell->center, child_radius))
				{
					if (clipped == FRUSTUM_CLIP_NONE)
					{
						WORLD_STAT(octree_cells_visible);
						search_cell->no_frustum_check = true;
					}
					else 
					{
						// remove the top flag because in return values from frustumCheckSphere,
						// it indicates not trivially accepted
						clipped &= ~FRUSTUM_CLIP_SPHERE_PARTIAL;

						clipped = !checkBoundsNotVisible(state, &child_cell->bounds, clipped, false, &child_cell->inherited_bits);
						if (!clipped)
						{
							WORLD_STAT(octree_cells_culled);
							--search_cell_count; // undo the dynArrayAddStruct
						}
					}
				}
				else
				{
					WORLD_STAT(octree_cells_culled);
					--search_cell_count; // undo the dynArrayAddStruct
				}


#if OCTREE_DEBUG_DRAW
				if ( debug_drawOctree && clipped )
				{
					extern void gfxDrawBox3DEx(const Vec3 min, const Vec3 max, const Mat4 mat, Color color, F32 line_width, int faceBits);
					Color red = { 255, 0, 0, 63 };
					Color green = { 0, 255, 0, 63 };
					Vec3 cell_min, cell_max, center;

					if (clipped != FRUSTUM_CLIP_NONE)
					{
						red.b = 255;
						green.r = 255;
					}

					mulVecMat4(search_cell->center, frustum->inv_viewmat, center);
					subVec3same(center, child_size, cell_min);
					addVec3same(center, child_size, cell_max);

					gfxDrawBox3DEx( child_cell->bounds.min, child_cell->bounds.max, 
						unitmat, red, 1, 63);
					gfxDrawBox3DEx( cell_min, cell_max, unitmat, green, 1, 63);
				}
#endif
			}
		}
	}

	octree->search_cell_dynarray = search_cell_dynarray;
	octree->search_cell_count = search_cell_count;
	octree->search_cell_max = search_cell_max;
}

static void octreeSplitPlanesFrustum_EnumAllVisibleChildren(Octree *octree,OctreeFindState *state)
{
	OctreeSearchCell *search_cell_dynarray = octree->search_cell_dynarray;
	int search_cell_count = octree->search_cell_count;
	int search_cell_max = octree->search_cell_max;
	int j;

	Vec3 state_cam_pos;
	F32 state_lod_scale;
	bool state_use_visdist;

	copyVec3(state->cam_pos, state_cam_pos);
	state_lod_scale = state->lod_scale;
	state_use_visdist = state->use_visdist;

	for (j = 0; j < search_cell_count; ++j)
	{
		OctreeSearchCell *search_cell = search_cell_dynarray + j;
		if (search_cell->no_frustum_check && search_cell->cell->children)
		{
			const OctreeCell * cell = search_cell->cell;
			int i;
			for (i = 0; i < OCT_NUMCELLS_CUBE; ++i)
			{
				OctreeCell *child_cell = cell->children[i];
				if (child_cell)
				{
					// vis_dist check
					F32 dist_sqr = distance3Squared(child_cell->bounds.mid, state_cam_pos);
					F32 vis_dist_sqr = child_cell->bounds.radius;
					vis_dist_sqr += state_lod_scale * child_cell->bounds.vis_dist;
					vis_dist_sqr = SQR(vis_dist_sqr);


					WORLD_STAT(octree_cells);

					if (state_use_visdist && dist_sqr > vis_dist_sqr)
					{
						WORLD_STAT(octree_cells_visradius_culled);
						continue;
					}

					child_cell->inherited_bits = cell->inherited_bits;

					search_cell = dynArrayAddStruct_no_memset(search_cell_dynarray, search_cell_count, search_cell_max);
					search_cell->cell = child_cell;
					search_cell->no_frustum_check = true;
				}
			}
		}
	}

	octree->search_cell_dynarray = search_cell_dynarray;
	octree->search_cell_count = search_cell_count;
	octree->search_cell_max = search_cell_max;
}

static void octreeSplitPlanesFrustumDA(Octree *octree,OctreeFindState *state,OctreeCell *parent_cell,Vec3 parent_center,int parent_no_frustum_check)
{
	int j;
	OctreeSearchCell *search_cell;
	int search_cell_count;
	int search_cell_max;

	PERFINFO_AUTO_START_FUNC();

	octreeSplitPlanesFrustum_GetNonCulledList(octree, state, parent_cell, parent_center);
	octreeSplitPlanesFrustum_EnumAllVisibleChildren(octree, state);

	WORLD_STAT_I(octree_search_cell_max, octree->search_cell_count);

	search_cell = octree->search_cell_dynarray;
	search_cell_count = octree->search_cell_count;
	search_cell_max = octree->search_cell_max;
	for (j = 0; j < search_cell_count; ++j, ++search_cell)
	{
		if (search_cell->cell->entries)
		{
			processOctreeEntriesDA(octree, search_cell->cell, state, search_cell->no_frustum_check);
		}
	}

	PERFINFO_AUTO_STOP();
}

void octreeFindInFrustumCB(Octree *octree, F32 lod_scale, bool use_visdist, OctreeFindCallback callback, const Vec3 cam_pos, const Frustum *frustum, OctreeOcclusionCallback occlusion_callback, GfxOcclusionBuffer *zo)
{
	Vec3 pos_cameraspace;
	int clipped;
	OctreeFindState	state = {0};
	F32 radius, dist_sqr, vis_dist_sqr;

	if (octree->need_vis_recompute)
	{
		cellRecomputeBounds(octree, octree->cell, true);
		octree->need_vis_recompute = false;
	}

	dist_sqr = distance3Squared(octree->cell->bounds.mid, cam_pos);
	vis_dist_sqr = lod_scale * octree->cell->bounds.vis_dist + octree->cell->bounds.radius;
	vis_dist_sqr = SQR(vis_dist_sqr);
	if (use_visdist && dist_sqr > vis_dist_sqr)
		return;

	radius = octree->cell->size * 1.73205f;
	state.frustum = frustum;
	state.callback = callback;
	state.occlusion_callback = occlusion_callback;
	state.user_data = zo;
	state.tag = ++octree->tag;
	state.octree = octree;
	state.lod_scale = lod_scale;
	state.use_visdist = use_visdist;
	copyVec3(cam_pos, state.cam_pos);
	mulVecMat4(octree->cell->center, frustum->viewmat, pos_cameraspace);
	if (clipped = frustumCheckSphere(frustum, pos_cameraspace, radius))
		octreeSplitPlanesFrustum(octree, &state, octree->cell, pos_cameraspace, clipped == FRUSTUM_CLIP_NONE);
}

void octreeFindInFrustumDA(Octree *octree, F32 lod_scale, bool use_visdist, const Vec3 cam_pos, const Frustum *frustum, OctreeOcclusionCallback occlusion_callback, GfxOcclusionBuffer *zo,
	OctreeDrawEntry **draw_entries, int *draw_entry_count, int *draw_entry_max)
{
	Vec3 pos_cameraspace;
	int clipped;
	OctreeFindState	state = {0};
	F32 radius, dist_sqr, vis_dist_sqr;

	if (octree->need_vis_recompute)
	{
		cellRecomputeBounds(octree, octree->cell, true);
		octree->need_vis_recompute = false;
	}

	dist_sqr = distance3Squared(octree->cell->bounds.mid, cam_pos);
	vis_dist_sqr = lod_scale * octree->cell->bounds.vis_dist + octree->cell->bounds.radius;
	vis_dist_sqr = SQR(vis_dist_sqr);
	if (use_visdist && dist_sqr > vis_dist_sqr)
		return;

	radius = octree->cell->size * 1.73205f;
	state.frustum = frustum;
	state.callback = NULL;
	state.occlusion_callback = occlusion_callback;
	state.user_data = zo;
	state.tag = ++octree->tag;
	state.octree = octree;
	state.lod_scale = lod_scale;
	state.use_visdist = use_visdist;
	state.octreeDrawData = *draw_entries;
	state.octreeDrawCount = 0;
	state.octreeDrawMax = *draw_entry_max;

	copyVec3(cam_pos, state.cam_pos);
	mulVecMat4(octree->cell->center, frustum->viewmat, pos_cameraspace);
	if (clipped = frustumCheckSphere(frustum, pos_cameraspace, radius))
		octreeSplitPlanesFrustumDA(octree, &state, octree->cell, pos_cameraspace, clipped == FRUSTUM_CLIP_NONE);

	*draw_entries = state.octreeDrawData;
	*draw_entry_count = state.octreeDrawCount;
	*draw_entry_max = state.octreeDrawMax;
}

void octreeFindInFrustumEA_dbg(Octree *octree, F32 lod_scale, bool use_visdist, void ***node_array, const Vec3 cam_pos, const Frustum *frustum, OctreeOcclusionCallback occlusion_callback, GfxOcclusionBuffer *zo MEM_DBG_PARMS)
{
	Vec3 pos_cameraspace;
	int clipped;
	OctreeFindState	state = {0};
	F32 radius, dist_sqr, vis_dist_sqr;

	if (octree->need_vis_recompute)
	{
		cellRecomputeBounds(octree, octree->cell, true);
		octree->need_vis_recompute = false;
	}

	dist_sqr = distance3Squared(octree->cell->bounds.mid, cam_pos);
	vis_dist_sqr = lod_scale * octree->cell->bounds.vis_dist + octree->cell->bounds.radius;
	vis_dist_sqr = SQR(vis_dist_sqr);
	if (use_visdist && dist_sqr > vis_dist_sqr)
		return;

	radius = octree->cell->size * 1.73205f;
	state.frustum = frustum;
	state.occlusion_callback = occlusion_callback;
	state.user_data = zo;
	state.nodes = node_array;
	state.tag = ++octree->tag;
	state.octree = octree;
	state.lod_scale = lod_scale;
	state.use_visdist = use_visdist;
	MEM_DBG_STRUCT_PARMS_INIT(&state);
	copyVec3(cam_pos, state.cam_pos);
	mulVecMat4(octree->cell->center, frustum->viewmat, pos_cameraspace);
	if (clipped = frustumCheckSphere(frustum, pos_cameraspace, radius))
		octreeSplitPlanesFrustum(octree, &state, octree->cell, pos_cameraspace, clipped == FRUSTUM_CLIP_NONE);
}

void **octreeFindInFrustum_dbg(Octree *octree, F32 lod_scale, bool use_visdist, const Vec3 cam_pos, const Frustum *frustum, OctreeOcclusionCallback occlusion_callback, GfxOcclusionBuffer *zo MEM_DBG_PARMS)
{
	void **nodes = NULL;
	Vec3 pos_cameraspace;
	int clipped;
	OctreeFindState	state = {0};
	F32 radius, dist_sqr, vis_dist_sqr;

	if (octree->need_vis_recompute)
	{
		cellRecomputeBounds(octree, octree->cell, true);
		octree->need_vis_recompute = false;
	}

	dist_sqr = distance3Squared(octree->cell->bounds.mid, cam_pos);
	vis_dist_sqr = lod_scale * octree->cell->bounds.vis_dist + octree->cell->bounds.radius;
	vis_dist_sqr = SQR(vis_dist_sqr);
	if (use_visdist && dist_sqr > vis_dist_sqr)
		return NULL;

	radius = octree->cell->size * 1.73205f;
	state.frustum = frustum;
	state.occlusion_callback = occlusion_callback;
	state.user_data = zo;
	state.nodes = &nodes;
	state.tag = ++octree->tag;
	state.octree = octree;
	state.lod_scale = lod_scale;
	state.use_visdist = use_visdist;
	MEM_DBG_STRUCT_PARMS_INIT(&state);
	copyVec3(cam_pos, state.cam_pos);
	mulVecMat4(octree->cell->center, frustum->viewmat, pos_cameraspace);
	if (clipped = frustumCheckSphere(frustum, pos_cameraspace, radius))
		octreeSplitPlanesFrustum(octree, &state, octree->cell, pos_cameraspace, clipped == FRUSTUM_CLIP_NONE);
	return nodes;
}

bool octreeTestBox(Octree *octree, const Vec3 bounds_min, const Vec3 bounds_max)
{
	if (octree->need_vis_recompute)
	{
		cellRecomputeBounds(octree, octree->cell, true);
		octree->need_vis_recompute = false;
	}

	return boxBoxCollision(bounds_min, bounds_max, octree->cell->bounds.min, octree->cell->bounds.max);
}

static int octreeForEachCellEntryInternal(OctreeCell *cell, OctreeForEachCallback callback, U32 tag)
{
	OctreeEntryPtr entry;
	OctreeEntries *ents;
	int count = 0, i;

	if (!cell)
		return 0;

	for (ents = cell->entries; ents; ents = ents->next)
	{
		for (i = 0; i < ARRAY_SIZE(ents->entries); ++i)
		{
			entry = ents->entries[i];
			if (!entry || entry->last_visited_tag == tag)
				continue;

			entry->last_visited_tag = tag;
			if (callback)
				callback(entry);
			++count;
		}
	}

	// recurse
	if (cell->children)
	{
		for (i = 0; i < OCT_NUMCELLS_CUBE; ++i)
			count += octreeForEachCellEntryInternal(cell->children[i], callback, tag);
	}

	return count;
}

int octreeForEachEntry(Octree *octree, OctreeForEachCallback callback)
{
	U32 tag = ++octree->tag;
	return octreeForEachCellEntryInternal(octree->cell, callback, tag);
}

void octreeFindInBoxEA_dbg( SA_PARAM_NN_VALID Octree *octree, void ***node_array, const Vec3 box_min, const Vec3 box_max, const Mat4 box_trans, const Mat4 box_trans_inv MEM_DBG_PARMS )
{
	OctreeFindState	state = {0};

	if (octree->need_vis_recompute)
	{
		cellRecomputeBounds(octree, octree->cell, true);
		octree->need_vis_recompute = false;
	}

	state.nodes = node_array;
	state.tag = ++octree->tag;
	state.octree = octree;
	state.use_box = true;
	copyVec3(box_min, state.box_min);
	copyVec3(box_max, state.box_max);
	copyMat4(box_trans, state.box_trans);
	copyMat4(box_trans_inv, state.box_trans_inv);
	MEM_DBG_STRUCT_PARMS_INIT(&state);

	octreeSplitPlanesBox(octree, &state, octree->cell);
}