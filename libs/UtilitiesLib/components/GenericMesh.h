#ifndef _GENERICMESH_H_
#define _GENERICMESH_H_
GCC_SYSTEM

#include "mathutil.h"
#include "gridpoly.h"
#include "earray.h"
#include "loggingEnums.h"

C_DECLARATIONS_BEGIN

typedef struct GConvexHull GConvexHull;
typedef struct SimpleBuffer* SimpleBufHandle;
typedef struct SimplygonMesh SimplygonMesh;

#include "GenericMeshBits.h"

typedef enum GMeshBooleanOp
{
	BOOLEAN_SPLIT,
	BOOLEAN_UNION,
	BOOLEAN_INTERSECT,
	BOOLEAN_SUBTRACT,
	BOOLEAN_SUBTRACT_NOFILL,
} GMeshBooleanOp;

typedef struct GTriIdx
{
	int idx[3];
	U16 tex_id;
} GTriIdx;

typedef U16 GBoneMats[4];

typedef struct GMesh
{
	int			usagebits;

	int			vert_count, vert_max;
	Vec3		*positions;
	Vec3		*positions2;
	Vec3		*normals;
	Vec3		*normals2;
	Vec3		*binormals;
	Vec3		*tangents;
	Vec2		*tex1s;
	Vec2		*tex2s;
	Vec4		*boneweights;
	GBoneMats	*bonemats;
	Color		*colors;
	int			varcolor_size;
	U8			*varcolors;

	int			tri_count, tri_max;
	GTriIdx		*tris;

	PolyGrid	grid;
	Color		dbg_color;

	bool		merge_average_tangent_basis;

	/*
	if making use of USE_OPTIONAL_PARSE_STRUCT in usagebits for writing extra data to a file,
	beware that the serialization is written in ascii.  If writing a large amount of data,
	try to find a more optimal solution.
	*/
	const char	*parse_table;
	void		*parse_struct_data;

	MEM_DBG_STRUCT_PARMS

} GMesh;

AUTO_STRUCT;
typedef struct GMeshParsed
{
	F32			*positions;
	F32			*positions2;
	F32			*normals;
	F32			*normals2;
	F32			*binormals;
	F32			*tangents;
	F32			*tex1s;
	F32			*tex2s;
	F32			*boneweights;
	int			*bonemats;
	int			*colors;
	int			*tris;

} GMeshParsed;

extern ParseTable parse_GMeshParsed[];
#define TYPE_parse_GMeshParsed GMeshParsed

typedef struct GMeshReductions
{
	int num_reductions;

	int *num_tris_left;  // one for each reduction step
	float *error_values; // one for each reduction step
	int *remaps_counts;  // number of tri remaps for each reduction step
	int *changes_counts; // number of vert changes for each reduction step

	// tri remaps
	int total_remaps;
	int *remaps; // sequence of [oldidx,newidx,numtris]
	int total_remap_tris;
	int *remap_tris;

	// vert changes
	int total_changes;
	int *changes; // vert idxs
	Vec3 *positions;
	Vec2 *tex1s;
	Vec3 *normals;
	U8 *varcolors;


} GMeshReductions;

typedef enum ReductionMethod { TRICOUNT_RMETHOD, ERROR_RMETHOD, COLLAPSE_COUNT_RMETHOD, TRICOUNT_AND_ERROR_RMETHOD } ReductionMethod;

//////////////////////////////////////////////////////////////////////////

void gmeshSetUsageBits_dbg(GMesh *mesh, int bits MEM_DBG_PARMS);
#define gmeshSetUsageBits(mesh, bits) gmeshSetUsageBits_dbg(mesh, bits MEM_DBG_PARMS_INIT)
void gmeshRemoveAttributes(GMesh * mesh, int removeUsageBits);
U32 gmeshGetVertexSize(const GMesh *mesh);
void gmeshSetVertCount_dbg(GMesh *mesh, int vcount MEM_DBG_PARMS);
#define gmeshSetVertCount(mesh, vcount) gmeshSetVertCount_dbg(mesh, vcount MEM_DBG_PARMS_INIT)
void gmeshEnsureTrisFit_dbg(GMesh *mesh, int tcount MEM_DBG_PARMS);
#define gmeshEnsureTrisFit(mesh, tcount) gmeshEnsureTrisFit_dbg(mesh, tcount MEM_DBG_PARMS_INIT)
void gmeshSetTriCount_dbg(GMesh *mesh, int tcount MEM_DBG_PARMS);
#define gmeshSetTriCount(mesh, tcount) gmeshSetTriCount_dbg(mesh, tcount MEM_DBG_PARMS_INIT)
void gmeshSetVarColorSize_dbg(GMesh *mesh, int varcolor_size MEM_DBG_PARMS);
#define gmeshSetVarColorSize(mesh, varcolor_size) gmeshSetVarColorSize_dbg(mesh, varcolor_size MEM_DBG_PARMS_INIT)

__forceinline static GMeshTangentSpaceState gmeshGetTangentBasisAttributeState(const GMesh * gmesh)
{
	GMeshTangentSpaceState state = TANGENT_SPACE_NONE;
	int gmts_usage_bits = gmesh->usagebits & USE_TANGENT_BASIS_BITS;
	if (gmts_usage_bits)
	{
		if (gmts_usage_bits != USE_TANGENT_BASIS_BITS && gmts_usage_bits != USE_NORMALS)
			// { normal, tangent }, { normal, binormal }, { tangent, binormal }, { tangent }, { binormal }, and so
			// are all incomplete bases. Technically, so is  { normal }, but it is an expected configuration for a 
			// model, so we call it no tangent basis.
			state = TANGENT_SPACE_INCOMPLETE;
		else
			state = TANGENT_SPACE_COMPLETE;
	}
	return state;
}

int gmeshNearSameVert(const GMesh *mesh, int idx, const Vec3 pos, const Vec3 pos2, const Vec3 norm, const Vec3 norm2, const Vec3 binorm, const Vec3 tangent, const Vec2 tex1, const Vec2 tex2, const Color *color, const U8 *varcolor, bool ignore_color_differences, bool ignore_color_alpha_differences);
void gmeshAddTex2s(GMesh *mesh, const GMesh *srcmesh);

void gmeshAddPositions2_dbg(GMesh *mesh, const GMesh *srcmesh MEM_DBG_PARMS);
#define gmeshAddPositions2(mesh, srcmesh) gmeshAddPositions2_dbg(mesh, srcmesh MEM_DBG_PARMS_INIT)
void gmeshAddNormals2_dbg(GMesh *mesh, const GMesh *srcmesh MEM_DBG_PARMS);
#define gmeshAddNormals2(mesh, srcmesh) gmeshAddNormals2_dbg(mesh, srcmesh MEM_DBG_PARMS_INIT)

int gmeshAddVert(GMesh *mesh, const Vec3 pos, const Vec3 pos2, const Vec3 norm, const Vec3 norm2, const Vec3 binorm, const Vec3 tangent, const Vec2 tex1, const Vec2 tex2, const Color *color, const U8 *varcolor, const Vec4 boneweight, const GBoneMats bonemat, int check_exists, bool ignore_color_differences, bool ignore_color_alpha_differences);
__forceinline static int gmeshAddVertSimple(GMesh *mesh, const Vec3 pos, const Vec3 norm, const Vec2 tex1, const Color *color, const Vec4 boneweight, const GBoneMats bonemat, int check_exists, bool ignore_color_differences, bool ignore_color_alpha_differences)
{
	return gmeshAddVert(mesh, pos, NULL, norm, NULL, NULL, NULL, tex1, NULL, color, NULL, boneweight, bonemat, check_exists, ignore_color_differences, ignore_color_alpha_differences);
}
__forceinline static int gmeshAddVertSimple2(GMesh *mesh, const Vec3 pos, const Vec3 norm, const Vec2 tex1, const Vec2 tex2, const Color *color, const Vec4 boneweight, const GBoneMats bonemat, int check_exists, bool ignore_color_differences, bool ignore_color_alpha_differences)
{
	return gmeshAddVert(mesh, pos, NULL, norm, NULL, NULL, NULL, tex1, tex2, color, NULL, boneweight, bonemat, check_exists, ignore_color_differences, ignore_color_alpha_differences);
}
int gmeshAddInterpolatedVert(GMesh *mesh, int idx1, int idx2, F32 ratio_1to2);
int gmeshAddTri(GMesh *mesh, int idx0, int idx1, int idx2, U16 tex_id, int check_exists);

__forceinline static void gmeshAddQuad(GMesh *mesh, int idx0, int idx1, int idx2, int idx3, U16 tex_id, int check_exists)
{
	gmeshAddTri(mesh, idx0, idx1, idx2, tex_id, check_exists);
	gmeshAddTri(mesh, idx0, idx2, idx3, tex_id, check_exists);
}


int gmeshSplitTri(GMesh *mesh, const Vec4 plane, int tri_idx); // returns 1 if triangle was split
void gmeshRemoveTri(GMesh *mesh, int tri_idx);
void gmeshMarkBadTri(GMesh *mesh, int tri_idx);
int gmeshMarkDegenerateTris(GMesh *mesh);
int gmeshSortTrisByTexID(GMesh *mesh, int (*cmp)(int *, int *));
void gmeshSortTrisByVertColor(GMesh *mesh);
void gmeshSort(GMesh *mesh);
void gmeshRemapVertex(GMesh *mesh, int old_idx, int new_idx);
void gmeshRemapTriVertex(GMesh *mesh, int tri_idx, int old_idx, int new_idx);
void gmeshGetTriangleBarycentricCoordinates(const GMesh *mesh, int tri_idx, const Vec3 position, Vec3 barycentric_coords);

void gmeshTransform(GMesh *mesh, const Mat4 mat);
int gmeshAddTangentSpace_dbg(GMesh *mesh, bool use_wind, const char * asset_filename, enumLogCategory log_destination, bool flip_binormal MEM_DBG_PARMS);
#define gmeshAddTangentSpace(mesh, use_wind, asset_filename, log_destination) gmeshAddTangentSpace_dbg(mesh, use_wind, asset_filename, log_destination, true MEM_DBG_PARMS_INIT)

F32 gmeshGetBounds(const GMesh *mesh, Vec3 local_min, Vec3 local_max, Vec3 local_mid);

GConvexHull *gmeshToGConvexHullEx(GMesh *mesh, int **tri_to_plane);
#define gmeshToGConvexHull(mesh) gmeshToGConvexHullEx(mesh, NULL);

void gmeshCopy_dbg(GMesh *dst, const GMesh *src, int fill_grid MEM_DBG_PARMS);
#define gmeshCopy(dst, src, fill_grid) gmeshCopy_dbg(dst, src, fill_grid MEM_DBG_PARMS_INIT)
void gmeshMerge_dbg(GMesh *dst, const GMesh *src, int pool_verts, int pool_tris, bool ignore_color_differences, bool ignore_color_alpha_differences MEM_DBG_PARMS);
#define gmeshMerge(dst, src, pool_verts, pool_tris, ignore_color_differences, ignore_color_alpha_differences) gmeshMerge_dbg(dst, src, pool_verts, pool_tris, ignore_color_differences, ignore_color_alpha_differences MEM_DBG_PARMS_INIT)
void gmeshFreeData(SA_PARAM_OP_VALID GMesh *mesh);
void gmeshFree(GMesh *mesh);

int gmeshGetMaterialCount(const GMesh *mesh);

void gmeshPool_dbg(GMesh *mesh, int pool_tris, bool ignore_color_differences, bool ignore_color_alpha_differences MEM_DBG_PARMS);
#define gmeshPool(mesh, pool_tris, ignore_color_differences, ignore_color_alpha_differences) gmeshPool_dbg(mesh, pool_tris, ignore_color_differences, ignore_color_alpha_differences MEM_DBG_PARMS_INIT)
void gmeshUnpool_dbg(GMesh *mesh, int pool_tris, bool ignore_color_differences, bool ignore_color_alpha_differences MEM_DBG_PARMS);
#define gmeshUnpool(mesh, pool_tris, ignore_color_differences, ignore_color_alpha_differences) gmeshUnpool_dbg(mesh, pool_tris, ignore_color_differences, ignore_color_alpha_differences MEM_DBG_PARMS_INIT)

bool gmeshRaycast(GMesh *mesh, const Vec3 ray_start, const Vec3 ray_vec, F32 *collision_time, Vec3 collision_tri_normal);

U32 gmeshCRC(const GMesh *mesh);
bool gmeshCompare(const GMesh *mesh1, const GMesh *mesh2);

#define GMESH_MIN_UV_DENSITY			0.00000095367431640625f		/* 2^-20 */
#define GMESH_LOG_MIN_UV_DENSITY		-10.0f	/* log4(GMESH_MIN_UV_DENSITY) */

F32 gmeshUvDensity(const GMesh *mesh);

// returns last error
F32 gmeshReducePrecalced_dbg(GMesh *dst, const GMesh *src, const GMeshReductions *reductions, float desired_error, ReductionMethod method MEM_DBG_PARMS);
#define gmeshReducePrecalced(dst, src, reductions, desired_error, method) gmeshReducePrecalced_dbg(dst, src, reductions, desired_error, method MEM_DBG_PARMS_INIT)
void freeGMeshReductions(GMeshReductions *gmr);
GMeshReductions *gmeshCalculateReductions(const GMesh *mesh, F32 distances[3], F32 scale_by_area, bool use_optimal_placement, bool maintain_borders, bool is_terrain);
F32 gmeshReduce_dbg(GMesh *dst, const GMesh *src, F32 target_error, F32 target_tricount, ReductionMethod method, F32 scale_by_area, F32 upscale, bool use_optimal_placement, bool maintain_borders, bool is_terrain, bool disable_simplygon, const char *debug_name, const char *error_filename MEM_DBG_PARMS);
#define gmeshReduceDebug(dst, src, target_error, target_tricount, method, scale_by_area, upscale, use_optimal_placement, maintain_borders, is_terrain, disable_simplygon, debug_name, error_filename) gmeshReduce_dbg(dst, src, target_error, target_tricount, method, scale_by_area, upscale, use_optimal_placement, maintain_borders, is_terrain, disable_simplygon, debug_name, error_filename MEM_DBG_PARMS_INIT)
#define gmeshReduce(dst, src, target_error, target_tricount, method, scale_by_area, upscale, use_optimal_placement, maintain_borders, is_terrain) gmeshReduce_dbg(dst, src, target_error, target_tricount, method, scale_by_area, upscale, use_optimal_placement, maintain_borders, is_terrain, false, NULL, NULL MEM_DBG_PARMS_INIT)
bool simplygonReduce(GMesh *dst, const GMesh *src, float maxError, float targetTriCount, float *actualError);
SimplygonMesh *simplygonMeshFromGMesh(const GMesh *src, int *texIdMappings, int numTexIdMappings, bool *overSizeLimit, const char *name, int usageBitMask);
bool simplygonMeshToGMesh(SimplygonMesh *mesh, GMesh *dst, int usageBitMask);
void simplygonSetupPath(void);
void simplygonSetEnabled(bool enabled);
bool simplygonGetEnabled(void);

void gmeshAddOptionalParseStruct(GMesh *mesh, ParseTable *pti, void* optParseData);
void gmeshInvertTriangles(GMesh *mesh);
void gmeshBoolean_dbg(GMesh *dst, GMesh *src_a, GMesh *src_b, GMeshBooleanOp boolean_op, bool do_op_inverted MEM_DBG_PARMS); // do_op_inverted inverts the triangles, does the op, then inverts them again
#define gmeshBoolean(dst, src_a, src_b, boolean_op, do_op_inverted) gmeshBoolean_dbg(dst, src_a, src_b, boolean_op, do_op_inverted MEM_DBG_PARMS_INIT)

void gmeshFromBoundingBox_dbg(GMesh *mesh, const Vec3 local_min, const Vec3 local_max, const Mat4 world_matrix MEM_DBG_PARMS);
#define gmeshFromBoundingBox(mesh, local_min, local_max, world_matrix) gmeshFromBoundingBox_dbg(mesh, local_min, local_max, world_matrix MEM_DBG_PARMS_INIT)
void gmeshFromBoundingBoxWithAttribs_dbg(GMesh *mesh, const Vec3 local_min, const Vec3 local_max, const Mat4 world_matrix MEM_DBG_PARMS);
#define gmeshFromBoundingBoxWithAttribs(mesh, local_min, local_max, world_matrix) gmeshFromBoundingBoxWithAttribs_dbg(mesh, local_min, local_max, world_matrix MEM_DBG_PARMS_INIT)

GMeshParsed *gmeshToParsedFormat(const GMesh *mesh);
int gmeshParsedGetSize(const GMeshParsed *mesh_parsed);
GMesh *gmeshFromParsedFormat_dbg(const GMeshParsed *mesh_parsed MEM_DBG_PARMS);
#define gmeshFromParsedFormat(mesh_parsed) gmeshFromParsedFormat_dbg(mesh_parsed MEM_DBG_PARMS_INIT)

U32 gmeshGetBinSize(const GMesh *mesh);
void gmeshWriteBinData(const GMesh *mesh, SimpleBufHandle buf);
GMesh *gmeshFromBinData_dbg(SimpleBufHandle buf MEM_DBG_PARMS);
#define gmeshFromBinData(buf) gmeshFromBinData_dbg(buf MEM_DBG_PARMS_INIT)

__forceinline static F32 gmeshGetTriArea(const GMesh *mesh, int tri_idx)
{
	GTriIdx *tri = &mesh->tris[tri_idx];
	return triArea3(mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]]);
}

__forceinline static int gmeshTrisMatch(GTriIdx *tri, int idx0, int idx1)
{
	if (tri->idx[0] == idx0 && tri->idx[1] == idx1)
		return 3;
	if (tri->idx[0] == idx1 && tri->idx[1] == idx0)
		return -3;
	if (tri->idx[1] == idx0 && tri->idx[2] == idx1)
		return 1;
	if (tri->idx[1] == idx1 && tri->idx[2] == idx0)
		return -1;
	if (tri->idx[2] == idx0 && tri->idx[0] == idx1)
		return 2;
	if (tri->idx[2] == idx1 && tri->idx[0] == idx0)
		return -2;
	return false;
}

__forceinline static bool gmeshTrisMatch1(GTriIdx *tri, int idx)
{
	return (tri->idx[0] == idx || tri->idx[1] == idx || tri->idx[2] == idx);
}


//////////////////////////////////////////////////////////////////////////
// tangent space functions

typedef struct TangentSpaceGenData
{
	int tri_count;
	const U16 *tri_indices;
	const GTriIdx *tris;

	int vert_count;

	const F32 *positions;
	U32 position_stride; // in bytes

	const F32 *normals;
	U32 normal_stride; // in bytes

	const F32 *texcoords;
	U32 texcoord_stride; // in bytes

	const U32 *bone_weights;
	U32 bone_weight_stride; // in bytes

	const F32 *float_bone_weights;
	U32 float_bone_weight_stride; // in bytes

	const U16 *bone_ids;
	U32 bone_id_stride; // in bytes

	F32 *binormals;
	U32 binormal_stride; // in bytes

	F32 *tangents;
	U32 tangent_stride; // in bytes

	Color *colors;
	U32 color_stride; // in bytes

	int unwelded_tangent_basis;

} TangentSpaceGenData;

int addTangentSpace(TangentSpaceGenData *data, bool flip_binormal);


//////////////////////////////////////////////////////////////////////////
// vertex hashing functions

#define VHASH_BUCKET_COUNT 20
#define VHASH_BUCKET_SIZE 8
#define VHASH_BUCKET_MULTIPLIER (1.f / VHASH_BUCKET_SIZE)

typedef struct VertexHash
{
	int *vert_buckets[VHASH_BUCKET_COUNT][VHASH_BUCKET_COUNT][VHASH_BUCKET_COUNT];
} VertexHash;

__forceinline static void hashVertex(const Vec3 pos, IVec3 hash_pos)
{
	Vec3 scaled_pos;
	int i;

	scaleVec3(pos, VHASH_BUCKET_MULTIPLIER, scaled_pos);
	qtruncVec3NoFPCWChange(scaled_pos, hash_pos);

	for (i = 0; i < 3; ++i)
	{
		while (hash_pos[i] < 0)
			hash_pos[i] += VHASH_BUCKET_COUNT;
		while (hash_pos[i] >= VHASH_BUCKET_COUNT)
			hash_pos[i] -= VHASH_BUCKET_COUNT;
	}
}

__forceinline static int *getVertexHashBucket(VertexHash *vhash, const IVec3 hash_pos)
{
	return vhash->vert_buckets[hash_pos[2]][hash_pos[1]][hash_pos[0]];
}

__forceinline static void addVertexToHashBucket(VertexHash *vhash, const IVec3 hash_pos, int idx)
{
	eaiPush(&vhash->vert_buckets[hash_pos[2]][hash_pos[1]][hash_pos[0]], idx);
}

__forceinline static void removeVertexFromHashBucket(VertexHash *vhash, const IVec3 hash_pos, int idx)
{
	eaiFindAndRemoveFast(&vhash->vert_buckets[hash_pos[2]][hash_pos[1]][hash_pos[0]], idx);
}

__forceinline static void freeVertexHashData(VertexHash *vhash)
{
	int i, j, k;

	for (k = 0; k < VHASH_BUCKET_COUNT; ++k)
	{
		for (j = 0; j < VHASH_BUCKET_COUNT; ++j)
		{
			for (i = 0; i < VHASH_BUCKET_COUNT; ++i)
			{
				eaiDestroy(&vhash->vert_buckets[k][j][i]);
			}
		}
	}
}

__forceinline static void gmeshSetMergeAverageTangentBasis(GMesh * mesh, bool enable_merge_average)
{
	mesh->merge_average_tangent_basis = enable_merge_average;
}

bool validateGConvexHull(const GConvexHull *hull);

//////////////////////////////////////////////////////////////////////////
// terrain mesh functions

U8 tmeshClassifyNormal(const Vec3 normal);
void tmeshFixupFaceClassifications(U8 *face_buffer, int size);

void tmeshCalcTexcoord(Vec2 texcoord, F32 x, F32 y, F32 z, U8 dir);
void tmeshCalcNormal(Vec3 normal, const Vec3 *all_face_normals[3][3], int fidx, int x, int z, int p, int k, int size);

C_DECLARATIONS_END

#endif//_GENERICMESH_H_

