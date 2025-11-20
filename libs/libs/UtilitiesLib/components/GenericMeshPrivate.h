#pragma once
GCC_SYSTEM

#include "GenericMesh.h"
#include "MemoryPool.h"
#include "quadric.h"
#include "earray.h"
#include "EString.h"
#include "file.h"

#define VECTOL 0.0005f

// VECDIRTOL ~= 8 degrees
#define VECDIRTOL 0.99f


#define TOTEXEDGE_COST 50
#define TEXEDGE_COST 100
#define BOUNDARY_COST 1000
#define LOCKED_COST 2000

#define DEBUG_CHECKS 0
#define ENABLE_LOGGING 0
#define CHECK_LOG 0
#define ENABLE_MESH_LOGGING 0

#define BUCKET_BITS 4
#define BUCKET_COUNT (1<<BUCKET_BITS)

#define SORT_PERIODICALLY 1
#define NO_SORTING 1

#define MIN_ERROR 1e-7

// edge is boundary if tris along edge have normals pointing more than 120 degrees apart
#define BOUNDARY_ANGLE_CUTOFF -0.5f

#define UNCOLLAPSIBLE_COST 0x80000000

#if DEBUG_CHECKS
#define DO_DEBUG_ASSERT(exp) assert(exp)
#else
#define DO_DEBUG_ASSERT(exp)
#endif

#if ENABLE_LOGGING
#define LOG(fmt, ...) if (tc->logfile) fprintf(tc->logfile, fmt "\n", ##__VA_ARGS__)
#elif CHECK_LOG
#define LOG(fmt, ...) if (tc->logfile) checkLogMatch(tc->logfile, fmt, ##__VA_ARGS__)
#else
#define LOG(fmt, ...)
#endif

// for individual verts
typedef F32				*IVec;
typedef void			*IQuadric;

// for super verts
typedef Vec3			SVec;
typedef Quadric3		SQuadric;
#define evaluateSQ		evaluateQ3
#define zeroSQ			zeroQ3
#define initFromTriSQ	initFromTriQ3
#define scaleBySQ		scaleByQ3
#define addToSQ			addToQ3
#define copySVec		copyVec3
#define mulSVecSVec		mulVecVec3
#define subSVec			subVec3


typedef struct VertRemaps
{
	// tri remaps
	int *remaps; // sequence of [oldidx,newidx,numtris]
	int *remap_tris;

	// vert changes
	int *changes; // vert idxs
	F32 *positions; // Vec3 array
	F32 *tex1s; // Vec2 array
	F32 *normals; // Vec3 array
	int *varcolors;
} VertRemaps;

typedef struct ReduceInstruction
{
	VertRemaps vremaps;
	int num_tris_left;
	F32 cost, error;
} ReduceInstruction;

typedef struct MeshReduction
{
	ReduceInstruction **instructions;
} MeshReduction;


typedef enum VertUse
{
	USE_BEST,
	USE_VERT1,
	USE_VERT2
} VertUse;

typedef struct IEdge
{
	int ivert1, ivert2, tex_id;
	struct SEdge *sedge;
} IEdge;

typedef struct SEdge
{
	int svert1, svert2;
	IEdge **iedges;
	int *faces;

	F32 cost;
	U32 array_index;
	U32 bucket_index : BUCKET_BITS;
	U32 uncollapsible : 1;
	U32 connects_boundaries : 1;
	U32 connects_creases : 1;
	U32 connects_texedges : 1;
	U32 is_boundary : 1;
	U32 is_crease : 1;
	U32 is_texedge : 1;

	IVec best;
	Vec3 bestv;
	VertUse vert_to_use;
} SEdge;

typedef struct IVertInfo
{
	struct SVertInfo *svert;
	IEdge **iedges;
	IQuadric global_error;
	IQuadric local_error;
	F32 local_error_value;
	IVec attributes;
	U32 is_dead : 1;
} IVertInfo;

typedef struct SVertInfo
{
	int svert;
	SVec attributes;
	Vec3 v;
	int *iverts;
	SEdge **sedges;
	int *faces;

	F32 tri_area;

	SQuadric local_error;
	F32 local_error_value;

	U32 is_dead : 1;		// vertex has been removed from the mesh, stop processing it
	U32 on_boundary : 1;	// vertex is on the edge of the mesh: it is part of an SEdge that has only one face
	U32 on_crease : 1;		// vertex is on a crease in the mesh: it is part of an SEdge that has only one face
	U32 on_texedge : 1;		// vertex is on a texture seam: it is part of an SEdge that has multiple IEdges
	U32 is_locked :1;		// vertex cannot be moved or removed from the mesh
} SVertInfo;

typedef struct TriInfo
{
	Vec3 orig_face_normal;
	Vec3 face_normal;
	F32 area;
	SEdge *sedges[3];
	U32 is_dead : 1;
} TriInfo;

typedef struct SEdgeBucket
{
	SEdge **super_edges;
	U32 *super_edge_costs;
} SEdgeBucket;

typedef void (*IVecFunc)(const IVec v1, const IVec v2, IVec r);
typedef F32 (*evaluateIQFunc)(const IQuadric q, const IVec v);
typedef void (*addIQFunc)(const IQuadric q1, const IQuadric q2, IQuadric res);
typedef int (*optimizeIQFunc)(const IQuadric q, IVec out);
typedef void (*initFromTriIQFunc)(IQuadric q, const IVec v1, const IVec v2, const IVec v3);
typedef void (*addToIQFunc)(const IQuadric q, IQuadric res);

typedef struct TriCutType
{
	GMesh *mesh;
	
	IVertInfo *ivertinfos;
	SVertInfo **svertinfos;
	TriInfo *triinfos;
	IEdge **internal_edges;
	SEdgeBucket edge_buckets[BUCKET_COUNT];

	F32 scale_by_area;
	bool use_optimal_placement;
	bool maintain_borders;
	bool is_terrain;
	F32 target_error;
	IVec multiplier;
	IVec bounds_min, bounds_max;

	F32 min_tri_area, one_over_max_tri_area;
	U32 min_cost;
	U32 bucket_cost_offset;
	U32 bucket_cost_divisor;
	int collapse_counter;
	int last_need_rebucket;
	bool force_resort;
	int live_tris;

	IVec zeroivec;

	IVecFunc subIVec;
	IVecFunc mulIVecIVec;
	
	evaluateIQFunc evaluateIQ;
	addIQFunc addIQ;
	optimizeIQFunc optimizeIQ;
	initFromTriIQFunc initFromTriIQ;
	addToIQFunc addToIQ;

	MP_DEFINE_MEMBER(IEdge);
	MP_DEFINE_MEMBER(SEdge);
	MP_DEFINE_MEMBER(SVertInfo);
	MP_DEFINE_MEMBER(ReduceInstruction);

	int icount, ivec_size, iquadric_size;
	MP_DEFINE_MEMBER(IVec);
	MP_DEFINE_MEMBER(IQuadric);

	int *faces_array1, *faces_array2;
	int *changed_faces;
	SEdge **changed_edges;
	int *changed_iverts;
	int *dead_idxs;
	int *new_idxs;

	VertexHash svert_hash;

	FILE *logfile;

	int orig_tri_count, orig_vert_count;

} TriCutType;


int gmeshAddVertInternal(GMesh *mesh, const Vec3 pos, const Vec3 pos2, const Vec3 norm, const Vec3 norm2, const Vec3 binorm, const Vec3 tangent, const Vec2 tex1, const Vec2 tex2, const Color *color, const U8 *varcolor, const Vec4 boneweight, const GBoneMats bonemat, int check_exists, VertexHash *vhash, int origcount, bool ignore_color_differences, bool ignore_color_alpha_differences);


SEdge *tcGetCheapestEdge(TriCutType *tc);
ReduceInstruction *collapseEdge(TriCutType *tc, SEdge *sedge);

bool edgeIsBoundary(TriCutType *tc, SEdge *e);
bool edgeIsCrease(TriCutType *tc, SEdge *e);

void setVertAttributes(TriCutType *tc, int idx);
void calculateTriInfo(TriCutType *tc, int idx, int init);
void computeAllQuadrics(TriCutType *tc);

void applyTerrainTexturingAndNormals(TriCutType *tc, GMesh *output, Vec3 *texmap, int texmap_res, F32 one_over_texmap_step MEM_DBG_PARMS);

void checkVerts(TriCutType *tc);
void checkEdges(TriCutType *tc);
void checkTris(TriCutType *tc);
void checkLogMatch(FILE *logfile, const char *fmt, ...);
void removeEdgeFromBucket(TriCutType *tc, SEdge *e);


// returns 0 for behind plane, 1 for on plane, 2 for in front of plane
__forceinline static int classifyPoint(const Vec3 pos, const Vec4 plane)
{
	float dist = distanceToPlane(pos, plane);
	if (dist > 0)
		return 2;
	return dist == 0;
}

#define TANGENT_SPACE_TOLERANCE 0.7071f

__forceinline static int gmeshNearSameVertInline(const GMesh *mesh, int idx, const Vec3 pos, const Vec3 pos2, const Vec3 norm, const Vec3 norm2, const Vec3 binorm, const Vec3 tangent, const Vec2 tex1, const Vec2 tex2, const Color *color, const U8 *varcolor, bool ignore_color_differences, bool ignore_color_alpha_differences,
	float tangent_space_tolerance)
{
	if (mesh->positions && pos && !nearSameVec3Tol(mesh->positions[idx], pos, VECTOL))
		return 0;
	if (mesh->positions2 && pos2 && !nearSameVec3Tol(mesh->positions2[idx], pos2, VECTOL))
		return 0;
	if (mesh->normals && norm && !nearSameVec3DirTol(mesh->normals[idx], norm, VECDIRTOL))
		return 0;
	if (mesh->normals2 && norm2 && !nearSameVec3DirTol(mesh->normals2[idx], norm2, VECDIRTOL))
		return 0;
	if (mesh->binormals && binorm && !nearSameVec3DirTol(mesh->binormals[idx], binorm, tangent_space_tolerance))
		return 0;
	if (mesh->tangents && tangent && !nearSameVec3DirTol(mesh->tangents[idx], tangent, tangent_space_tolerance))
		return 0;
	if (mesh->tex1s && tex1 && !nearSameVec2(mesh->tex1s[idx], tex1))
		return 0;
	if (mesh->tex2s && tex2 && !nearSameVec2(mesh->tex2s[idx], tex2))
		return 0;
	if (!ignore_color_differences)
	{
		if (mesh->colors && color && mesh->colors[idx].integer_for_equality_only != color->integer_for_equality_only)
		{
			if (!ignore_color_alpha_differences)
				return 0;
			if (mesh->colors[idx].r != color->r || mesh->colors[idx].g != color->g || mesh->colors[idx].b != color->b)
				return 0;
		}
		if (mesh->varcolors && varcolor && memcmp(&mesh->varcolors[idx*mesh->varcolor_size], varcolor, mesh->varcolor_size * sizeof(U8))!=0)
			return 0;
	}
	return 1;
}

__forceinline static int sameTri(const GMesh *mesh, int tri_idx, int idx0, int idx1, int idx2, U16 tex_id)
{
	GTriIdx *tri = &mesh->tris[tri_idx];
	if (tri->tex_id != tex_id)
		return 0;
	if (tri->idx[0] == idx0 && tri->idx[1] == idx1 && tri->idx[2] == idx2)
		return 1;
	if (tri->idx[1] == idx0 && tri->idx[2] == idx1 && tri->idx[0] == idx2)
		return 1;
	if (tri->idx[2] == idx0 && tri->idx[0] == idx1 && tri->idx[1] == idx2)
		return 1;
	return 0;
}

__forceinline static bool isDegenerate(const Vec3 a, const Vec3 b, const Vec3 c)
{
	int count = 0;

	if (nearSameVec3Tol(a, b, VECTOL) || nearSameVec3Tol(a, c, VECTOL) || nearSameVec3Tol(b, c, VECTOL))
		return true;

	if (a[0] == b[0] && b[0] == c[0])
		count++;

	if (a[1] == b[1] && b[1] == c[1])
		count++;

	if (a[2] == b[2] && b[2] == c[2])
		count++;

	return count > 1;
}

__forceinline static bool isTriDegenerate(TriCutType *tc, GTriIdx *tri)
{
	GMesh *mesh = tc->mesh;

	if (tri->idx[0] < 0 || tri->idx[1] < 0 || tri->idx[2] < 0)
		return true;

	if (tri->idx[0] == tri->idx[1] || tri->idx[0] == tri->idx[2] || tri->idx[1] == tri->idx[2])
		return true;

	return isDegenerate(tc->ivertinfos[tri->idx[0]].svert->v, tc->ivertinfos[tri->idx[1]].svert->v, tc->ivertinfos[tri->idx[2]].svert->v);
}

__forceinline static bool edgeMatch(IEdge *e, int vert1, int vert2, int tex_id)
{
	if (e->tex_id != tex_id)
		return false;
	if (e->ivert1 == vert1 && e->ivert2 == vert2)
		return true;
	if (e->ivert2 == vert1 && e->ivert1 == vert2)
		return true;
	return false;
}

__forceinline static void freeSVert(TriCutType *tc, SVertInfo *svi)
{
	eaiDestroy(&svi->faces);
	eaiDestroy(&svi->iverts);
	eaDestroy(&svi->sedges);
	MP_FREE_MEMBER(tc, SVertInfo, svi);
}

__forceinline static void freeSEdge(TriCutType *tc, SEdge *e)
{
	eaiDestroy(&e->faces);
	eaDestroy(&e->iedges);
	MP_FREE_MEMBER(tc, IVec, e->best);
	MP_FREE_MEMBER(tc, SEdge, e);
}

__forceinline static void freeIEdge(TriCutType *tc, IEdge *e)
{
	eaFindAndRemoveFast(&e->sedge->iedges, e);
	MP_FREE_MEMBER(tc, IEdge, e);
}

__forceinline static int imin3(F32 a, F32 b, F32 c)
{
	int ia, ib, ic, im;
	ia = round(floorf(a));
	ib = round(floorf(b));
	ic = round(floorf(c));
	im = MIN(ia, ib);
	MIN1(im, ic);
	return im;
}

__forceinline static int imax3(F32 a, F32 b, F32 c)
{
	int ia, ib, ic, im;
	ia = round(ceilf(a));
	ib = round(ceilf(b));
	ic = round(ceilf(c));
	im = MAX(ia, ib);
	MAX1(im, ic);
	return im;
}

__forceinline static void eaFindAndRemoveFastInline(void ***handle, void *ptr)
{
	if (*handle)
	{
		EArray *earray = EArrayFromHandle(*handle);
		int i;
		for (i = 0; i < earray->count; ++i)
		{
			if (earray->structptrs[i] == ptr)
			{
				earray->structptrs[i] = earray->structptrs[--earray->count];
				break;
			}
		}
	}
}

__forceinline static void eaiFindAndRemoveFastInline(int **handle, int val)
{
	if (*handle)
	{
		EArray32 *earray = EArray32FromHandle(*handle);
		int i;
		for (i = 0; i < earray->count; ++i)
		{
			if (((int *)earray->values)[i] == val)
			{
				earray->values[i] = earray->values[--earray->count];
				break;
			}
		}
	}
}

__forceinline static int eaiPushUniqueInline_dbg(int **handle, int val MEM_DBG_PARMS)
{
	if (*handle)
	{
		EArray32 *earray = EArray32FromHandle(*handle);
		int i;
		for (i = 0; i < earray->count; ++i)
		{
			if (((int *)earray->values)[i] == val)
				return i;
		}
	}

	return ea32Push_dbg(handle, val MEM_DBG_PARMS_CALL);
}

#define eaiPushUniqueInline(handle, val) eaiPushUniqueInline_dbg(handle, val MEM_DBG_PARMS_INIT)

