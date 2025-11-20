#ifndef _OCTREE_H_
#define _OCTREE_H_
#pragma once
GCC_SYSTEM

#include "stdtypes.h"

typedef struct Frustum Frustum;
typedef struct GfxOcclusionBuffer GfxOcclusionBuffer;

typedef struct Octree Octree;
typedef struct OctreeIdxList OctreeIdxList;

typedef struct OctreeVisBounds
{
	struct 
	{
		U32					last_update_time;
		U8					occluded_bits;
	} occlusion_data;

	// externally set
	Vec3 mid;
	F32 radius;
	Vec3 min, max;
	F32 vis_dist;
} OctreeVisBounds;

typedef struct OctreeEntry
{
	// internal use
	U32 last_visited_tag;

	// required to be filled by the user
	OctreeVisBounds bounds;
	void *node;

	// optional, what these values mean is determined by the user
	int node_type;
	void *node_meta_data;

	// internal use
	OctreeIdxList *idx_list;
	Octree *octree;

} OctreeEntry;

typedef OctreeEntry *OctreeEntryPtr;

typedef struct OctreeDrawEntry
{
	OctreeEntryPtr entry;
	U8 occlusion_inherited_bits;
} OctreeDrawEntry;

typedef enum OctreeGranularity
{
	OCT_FINE_GRANULARITY,				// Smallest block = 4ft. (from Octree.c, line 466)
	OCT_MEDIUM_GRANULARITY,				// Smallest block = 16ft.
	OCT_ROUGH_GRANULARITY,				// Smallest block = 64ft.
	OCT_VERY_ROUGH_GRANULARITY,			// Smallest block = 512ft.
} OctreeGranularity;


SA_RET_NN_VALID Octree *octreeCreateDebug(SA_PRE_OP_RELEMS(3) const Vec3 center, F32 size, int mempool_size, const char *filename, int linenumber);
#define octreeCreateEx(center, size, mempool_size) octreeCreateDebug(center, size, mempool_size, __FILE__, __LINE__)
#define octreeCreate() octreeCreateEx(NULL, 0, 0)
void octreeDestroy(SA_PARAM_OP_VALID Octree *octree);

void octreeAddEntry(SA_PARAM_NN_VALID Octree *octree, SA_PARAM_NN_VALID OctreeEntry *entry, OctreeGranularity granularity);

__forceinline static bool octreeEntryInUse(SA_PARAM_NN_VALID OctreeEntry *entry)
{
	return entry->octree && entry->idx_list;
}

void octreeRemove(SA_PARAM_OP_VALID OctreeEntry *entry);
void octreeRemoveAll(SA_PARAM_OP_VALID Octree *octree);

typedef void (*OctreeFindCallback)(const OctreeVisBounds *bounds, void *node, int node_type, void *node_meta_data, U8 occlusion_inherited_bits);
typedef int (*OctreeSphereVisCallback)(void *node, int node_type, const Vec3 scenter, F32 sradius, void *user_data);
typedef int (*OctreeOcclusionCallback)(GfxOcclusionBuffer *zo, Vec4 eye_bounds[8], int isNearClipped, U32 *last_update_time, U8 *occluded_bits, U8 *inherited_bits, F32 *screen_space, bool *occlusionReady);
typedef void (*OctreeForEachCallback)(OctreeEntry *entry);

// the find in sphere functions do not use the bounds vis_dist
void octreeFindInSphereCB(SA_PARAM_NN_VALID Octree *octree, OctreeFindCallback callback, const Vec3 point, F32 radius, OctreeSphereVisCallback vis_callback, void *user_data);
void octreeFindInSphereEA_dbg(SA_PARAM_NN_VALID Octree *octree, void ***node_array, const Vec3 point, F32 radius, OctreeSphereVisCallback vis_callback, void *user_data MEM_DBG_PARMS);
void **octreeFindInSphere_dbg(SA_PARAM_NN_VALID Octree *octree, const Vec3 point, F32 radius, OctreeSphereVisCallback vis_callback, void *user_data MEM_DBG_PARMS);

#define octreeFindInSphereEA(octree, node_array, point, radius, vis_callback, user_data) octreeFindInSphereEA_dbg(octree, node_array, point, radius, vis_callback, user_data MEM_DBG_PARMS_INIT)
#define octreeFindInSphere(octree, point, radius, vis_callback, user_data) octreeFindInSphere_dbg(octree, point, radius, vis_callback, user_data MEM_DBG_PARMS_INIT)

// the find in frustum function use the bounds vis_dist
void octreeFindInFrustumCB(SA_PARAM_NN_VALID Octree *octree, F32 lod_scale, bool use_visdist, OctreeFindCallback callback, SA_PRE_NN_RELEMS(3) const Vec3 cam_pos, const Frustum *frustum, OctreeOcclusionCallback occlusion_callback, GfxOcclusionBuffer *zo);
void octreeFindInFrustumEA_dbg(SA_PARAM_NN_VALID Octree *octree, F32 lod_scale, bool use_visdist, void ***node_array,  SA_PRE_NN_RELEMS(3) const Vec3 cam_pos, const Frustum *frustum, OctreeOcclusionCallback occlusion_callback, GfxOcclusionBuffer *zo MEM_DBG_PARMS);
void **octreeFindInFrustum_dbg(SA_PARAM_NN_VALID Octree *octree, F32 lod_scale, bool use_visdist, SA_PRE_NN_RELEMS(3) const Vec3 cam_pos, const Frustum *frustum, OctreeOcclusionCallback occlusion_callback, GfxOcclusionBuffer *zo MEM_DBG_PARMS);
void octreeFindInFrustumDA(Octree *octree, F32 lod_scale, bool use_visdist, const Vec3 cam_pos, const Frustum *frustum, OctreeOcclusionCallback occlusion_callback, GfxOcclusionBuffer *zo,
	OctreeDrawEntry **draw_entries, int *draw_entry_count, int *draw_entry_max);

#define octreeFindInFrustumEA(octree, lod_scale, use_visdist, node_array, cam_pos, frustum, occlusion_callback, zo) octreeFindInFrustumEA_dbg(octree, lod_scale, use_visdist, node_array, cam_pos, frustum, occlusion_callback, zo MEM_DBG_PARMS_INIT)
#define octreeFindInFrustum(octree, lod_scale, use_visdist, cam_pos, frustum, occlusion_callback, zo) octreeFindInFrustum_dbg(octree, lod_scale, use_visdist, cam_pos, frustum, occlusion_callback, zo MEM_DBG_PARMS_INIT)

bool octreeTestBox(SA_PARAM_NN_VALID Octree *octree, SA_PRE_NN_RELEMS(3) const Vec3 bounds_min, SA_PRE_NN_RELEMS(3) const Vec3 bounds_max);

int octreeForEachEntry(SA_PARAM_NN_VALID Octree *octree, OctreeForEachCallback callback);

void octreeFindInBoxEA_dbg(SA_PARAM_NN_VALID Octree *octree, void ***node_array, const Vec3 box_min, const Vec3 box_max, const Mat4 box_trans, const Mat4 box_trans_inv MEM_DBG_PARMS);

#define octreeFindInBoxEA(octree, node_array, box_min, box_max, box_trans, box_trans_inv) octreeFindInBoxEA_dbg(octree, node_array, box_min, box_max, box_trans, box_trans_inv MEM_DBG_PARMS_INIT)

#endif //_OCTREE_H_
