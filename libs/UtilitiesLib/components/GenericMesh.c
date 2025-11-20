#include "GenericMesh.h"
#include "GenericMeshPrivate.h"
#include "GenericPoly.h"
#include "crypt.h"
#include "ScratchStack.h"
#include "textparser.h"
#include "Color.h"
#include "tritri_isectline.h"
#include "rand.h"
#include "DebugState.h"
#include "serialize.h"
#include "logging.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););

#define DEBUG_SPLIT 0

#if 0
#define CHECKHEAP 	{assertHeapValidateAll();}
#else
#define CHECKHEAP 	{}
#endif

#define ReallocStructs(ptr, count, mesh) strealloc((ptr), (count) * sizeof(*(ptr)), mesh)

#define EPSILON 0.001f

int maxTriangleSplits = -1;


// debug command for limiting the number of triangle splits that happen in a boolean operation to track down which one is causing problems
AUTO_CMD_INT(maxTriangleSplits, maxTriangleSplits);

void gmeshSetUsageBits_dbg(GMesh *mesh, int bits MEM_DBG_PARMS)
{
	if (!mesh->caller_fname)
	{
		MEM_DBG_STRUCT_PARMS_INIT(mesh);
	}

	if (mesh->usagebits == bits)
		return;

	mesh->usagebits = bits;

	if (!mesh->vert_max)
		return;

	if (!(bits & USE_POSITIONS))
	{
		SAFE_FREE(mesh->positions);
	}
	else if (!mesh->positions)
	{
		mesh->positions = ReallocStructs(mesh->positions, mesh->vert_max, mesh);
	}

	if (!(bits & USE_POSITIONS2))
	{
		SAFE_FREE(mesh->positions2);
	}
	else if (!mesh->positions2)
	{
		mesh->positions2 = ReallocStructs(mesh->positions2, mesh->vert_max, mesh);
	}

	if (!(bits & USE_NORMALS))
	{
		SAFE_FREE(mesh->normals);
	}
	else if (!mesh->normals)
	{
		mesh->normals = ReallocStructs(mesh->normals, mesh->vert_max, mesh);
	}

	if (!(bits & USE_NORMALS2))
	{
		SAFE_FREE(mesh->normals2);
	}
	else if (!mesh->normals2)
	{
		mesh->normals2 = ReallocStructs(mesh->normals2, mesh->vert_max, mesh);
	}

	if (!(bits & USE_BINORMALS))
	{
		SAFE_FREE(mesh->binormals);
	}
	else if (!mesh->binormals)
	{
		mesh->binormals = ReallocStructs(mesh->binormals, mesh->vert_max, mesh);
	}

	if (!(bits & USE_TANGENTS))
	{
		SAFE_FREE(mesh->tangents);
	}
	else if (!mesh->tangents)
	{
		mesh->tangents = ReallocStructs(mesh->tangents, mesh->vert_max, mesh);
	}

	if (!(bits & USE_TEX1S))
	{
		SAFE_FREE(mesh->tex1s);
	}
	else if (!mesh->tex1s)
	{
		mesh->tex1s = ReallocStructs(mesh->tex1s, mesh->vert_max, mesh);
	}

	if (!(bits & USE_TEX2S))
	{
		SAFE_FREE(mesh->tex2s);
	}
	else if (!mesh->tex2s)
	{
		mesh->tex2s = ReallocStructs(mesh->tex2s, mesh->vert_max, mesh);
	}

	if (!(bits & USE_BONEWEIGHTS))
	{
		SAFE_FREE(mesh->boneweights);
		SAFE_FREE(mesh->bonemats);
	}
	else if (!mesh->boneweights)
	{
		mesh->boneweights = ReallocStructs(mesh->boneweights, mesh->vert_max, mesh);
		mesh->bonemats = ReallocStructs(mesh->bonemats, mesh->vert_max, mesh);
	}

	if (!(bits & USE_COLORS))
	{
		SAFE_FREE(mesh->colors);
	}
	else if (!mesh->colors)
	{
		mesh->colors = ReallocStructs(mesh->colors, mesh->vert_max, mesh);
	}

	if (!(bits & USE_VARCOLORS))
	{
		SAFE_FREE(mesh->varcolors);
	}
	else if (!mesh->varcolors)
	{
		if (!mesh->varcolor_size)
			mesh->varcolor_size = 1;
		mesh->varcolors = ReallocStructs(mesh->varcolors, mesh->vert_max * mesh->varcolor_size, mesh);
	}

	if (!(bits & USE_OPTIONAL_PARSE_STRUCT))
	{
		if (mesh->parse_struct_data)
			StructDestroyVoid(ParserGetTableFromStructName(mesh->parse_table),mesh->parse_struct_data);
		mesh->parse_table = NULL;
	}
}

void gmeshRemoveAttributes(GMesh * mesh, int removeUsageBits)
{
	gmeshSetUsageBits(mesh, mesh->usagebits & ~removeUsageBits);
}

U32 gmeshGetVertexSize(const GMesh *mesh)
{
	U32 total_size = 0;
	int mesh_usagebits = mesh->usagebits;
	if (mesh_usagebits & USE_POSITIONS)
		total_size += sizeof(Vec3);
	if (mesh_usagebits & USE_POSITIONS2)
		total_size += sizeof(Vec3);
	if (mesh_usagebits & USE_NORMALS)
		total_size += sizeof(Vec3);
	if (mesh_usagebits & USE_NORMALS2)
		total_size += sizeof(Vec3);
	if (mesh_usagebits & USE_BINORMALS)
		total_size += sizeof(Vec3);
	if (mesh_usagebits & USE_TANGENTS)
		total_size += sizeof(Vec3);
	if (mesh_usagebits & USE_TEX1S)
		total_size += sizeof(Vec2);
	if (mesh_usagebits & USE_TEX2S)
		total_size += sizeof(Vec2);
	if (mesh_usagebits & USE_BONEWEIGHTS)
	{
		total_size += sizeof(Vec4);
		total_size += 4 * sizeof(U16);
	}
	if (mesh_usagebits & USE_COLORS)
		total_size += 4 * sizeof(U8);
	if (mesh_usagebits & USE_VARCOLORS)
		total_size += mesh->varcolor_size * sizeof(U8);
	return total_size;
}

void gmeshSetVertCount_dbg(GMesh *mesh, int vcount MEM_DBG_PARMS)
{
//	assert(vcount <= U16_MAX);

	if (!mesh->caller_fname)
	{
		MEM_DBG_STRUCT_PARMS_INIT(mesh);
	}

	if (vcount > mesh->vert_max)
	{
		int last_vmax = mesh->vert_max;
		mesh->vert_max = vcount << 1;

		if (mesh->usagebits & USE_POSITIONS)
		{
			mesh->positions = ReallocStructs(mesh->positions, mesh->vert_max, mesh);
			ZeroStructs(&mesh->positions[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_POSITIONS2)
		{
			mesh->positions2 = ReallocStructs(mesh->positions2, mesh->vert_max, mesh);
			ZeroStructs(&mesh->positions2[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_NORMALS)
		{
			mesh->normals = ReallocStructs(mesh->normals, mesh->vert_max, mesh);
			ZeroStructs(&mesh->normals[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_NORMALS2)
		{
			mesh->normals2 = ReallocStructs(mesh->normals2, mesh->vert_max, mesh);
			ZeroStructs(&mesh->normals2[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_BINORMALS)
		{
			mesh->binormals = ReallocStructs(mesh->binormals, mesh->vert_max, mesh);
			ZeroStructs(&mesh->binormals[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_TANGENTS)
		{
			mesh->tangents = ReallocStructs(mesh->tangents, mesh->vert_max, mesh);
			ZeroStructs(&mesh->tangents[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_TEX1S)
		{
			mesh->tex1s = ReallocStructs(mesh->tex1s, mesh->vert_max, mesh);
			ZeroStructs(&mesh->tex1s[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_TEX2S)
		{
			mesh->tex2s = ReallocStructs(mesh->tex2s, mesh->vert_max, mesh);
			ZeroStructs(&mesh->tex2s[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_BONEWEIGHTS)
		{
			mesh->boneweights = ReallocStructs(mesh->boneweights, mesh->vert_max, mesh);
			ZeroStructs(&mesh->boneweights[last_vmax], mesh->vert_max - last_vmax);

			mesh->bonemats = ReallocStructs(mesh->bonemats, mesh->vert_max, mesh);
			ZeroStructs(&mesh->bonemats[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_COLORS)
		{
			mesh->colors = ReallocStructs(mesh->colors, mesh->vert_max, mesh);
			ZeroStructs(&mesh->colors[last_vmax], mesh->vert_max - last_vmax);
		}

		if (mesh->usagebits & USE_VARCOLORS)
		{
			if (!mesh->varcolor_size)
				mesh->varcolor_size = 1;
			mesh->varcolors = ReallocStructs(mesh->varcolors, mesh->vert_max * mesh->varcolor_size, mesh);
			ZeroStructs(&mesh->varcolors[last_vmax * mesh->varcolor_size], (mesh->vert_max - last_vmax) * mesh->varcolor_size);
		}
	}

	mesh->vert_count = vcount;

	if (mesh->grid.cell)
		polyGridFree(&mesh->grid);
}

void gmeshEnsureTrisFit_dbg(GMesh *mesh, int tcount MEM_DBG_PARMS)
{
	if (!mesh->caller_fname)
	{
		MEM_DBG_STRUCT_PARMS_INIT(mesh);
	}

	if (tcount > mesh->tri_max)
	{
		int last_tmax = mesh->tri_max;
		mesh->tri_max = tcount;
		mesh->tris = ReallocStructs(mesh->tris, mesh->tri_max, mesh);
		ZeroStructs(&mesh->tris[last_tmax], mesh->tri_max - last_tmax);
	}
}

void gmeshSetTriCount_dbg(GMesh *mesh, int tcount MEM_DBG_PARMS)
{
	if (tcount > mesh->tri_max)
		gmeshEnsureTrisFit_dbg(mesh, tcount << 1 MEM_DBG_PARMS_CALL);

	mesh->tri_count = tcount;

	if (mesh->grid.cell)
		polyGridFree(&mesh->grid);
}

void gmeshSetVarColorSize_dbg(GMesh *mesh, int varcolor_size MEM_DBG_PARMS)
{
	U8 *old_varcolors = mesh->varcolors;
	int old_size = mesh->varcolor_size;

	mesh->varcolor_size = varcolor_size;
	if (old_size == mesh->varcolor_size || !mesh->vert_max || !(mesh->usagebits & USE_VARCOLORS))
		return;

	mesh->varcolors = stcalloc(mesh->vert_max * mesh->varcolor_size, sizeof(U8), mesh);

	if (old_varcolors)
	{
		int i, j, min_size = MIN(old_size, mesh->varcolor_size);

		for (i = 0; i < mesh->vert_count; ++i)
		{
			for (j = 0; j < min_size; ++j)
				mesh->varcolors[i*mesh->varcolor_size + j] = old_varcolors[i*old_size + j];
		}

		free(old_varcolors);
	}
}

void gmeshAddPositions2_dbg(GMesh *mesh, const GMesh *srcmesh MEM_DBG_PARMS)
{
	if (!mesh || !srcmesh || mesh->vert_count != srcmesh->vert_count)
		return;
	
	if (!srcmesh->positions && !srcmesh->positions2)
		return;

	gmeshSetUsageBits_dbg(mesh, (mesh->usagebits | USE_POSITIONS2) MEM_DBG_PARMS_CALL);

	if (srcmesh->positions2)
		CopyStructs(mesh->positions2, srcmesh->positions2, mesh->vert_count);
	else
		CopyStructs(mesh->positions2, srcmesh->positions, mesh->vert_count);
}

void gmeshAddNormals2_dbg(GMesh *mesh, const GMesh *srcmesh MEM_DBG_PARMS)
{
	if (!mesh || !srcmesh || mesh->vert_count != srcmesh->vert_count)
		return;

	if (!srcmesh->normals && !srcmesh->normals2)
		return;

	gmeshSetUsageBits_dbg(mesh, (mesh->usagebits | USE_NORMALS2) MEM_DBG_PARMS_CALL);

	if (srcmesh->normals2)
		CopyStructs(mesh->normals2, srcmesh->normals2, mesh->vert_count);
	else
		CopyStructs(mesh->normals2, srcmesh->normals, mesh->vert_count);
}

void gmeshAddTex2s(GMesh *mesh, const GMesh *srcmesh)
{
	if (!mesh || !srcmesh || mesh->vert_count != srcmesh->vert_count)
		return;

	if (!srcmesh->tex1s && !srcmesh->tex2s)
		return;

	gmeshSetUsageBits(mesh, mesh->usagebits | USE_TEX2S);

	if (srcmesh->tex2s)
		CopyStructs(mesh->tex2s, srcmesh->tex2s, mesh->vert_count);
	else
		CopyStructs(mesh->tex2s, srcmesh->tex1s, mesh->vert_count);
}

void gmeshAddOptionalParseStruct(GMesh *mesh, ParseTable *pti, void* optParseData)
{
	mesh->usagebits |= USE_OPTIONAL_PARSE_STRUCT;
	mesh->parse_struct_data = StructCreateVoid(pti);
	StructCopyVoid(pti,optParseData,mesh->parse_struct_data,0,0,0);
	mesh->parse_table = ParserGetTableName(pti);
}

int gmeshNearSameVert(const GMesh *mesh, int idx, const Vec3 pos, const Vec3 pos2, const Vec3 norm, const Vec3 norm2, const Vec3 binorm, const Vec3 tangent, const Vec2 tex1, const Vec2 tex2, const Color *color, const U8 *varcolor, bool ignore_color_differences, bool ignore_color_alpha_differences)
{
	return gmeshNearSameVertInline(mesh, idx, pos, pos2, norm, norm2, binorm, tangent, tex1, tex2, color, varcolor, ignore_color_differences, ignore_color_alpha_differences, VECDIRTOL);
}

int gmeshAddVertInternal(GMesh *mesh, const Vec3 pos, const Vec3 pos2, 
						 const Vec3 norm, const Vec3 norm2, 
						 const Vec3 binorm, const Vec3 tangent, 
						 const Vec2 tex1, const Vec2 tex2, 
						 const Color *color, const U8 *varcolor, 
						 const Vec4 boneweight, const GBoneMats bonemat, 
						 int check_exists, VertexHash *vhash, int origcount, 
						 bool ignore_color_differences, bool ignore_color_alpha_differences)
{
	IVec3 hash_pos;
	int i, idx;

	if (check_exists && vhash && pos)
		hashVertex(pos, hash_pos);

	if (check_exists)
	{
		if (vhash && mesh->positions && pos)
		{
			int *bucket = getVertexHashBucket(vhash, hash_pos);

			for (i = 0; i < eaiSize(&bucket); ++i)
			{
				if (gmeshNearSameVertInline(mesh, bucket[i], pos, pos2, norm, norm2, binorm, tangent, tex1, tex2, color, varcolor, ignore_color_differences, ignore_color_alpha_differences, TANGENT_SPACE_TOLERANCE))
				{
					int existing_vertex = bucket[i];
					if (mesh->merge_average_tangent_basis)
					{
						// add contribution from the merging vertex
						if (mesh->normals)
							addVec3(mesh->normals[existing_vertex], norm, mesh->normals[existing_vertex]);
						if (mesh->binormals)
							addVec3(mesh->binormals[existing_vertex], binorm, mesh->binormals[existing_vertex]);
						if (mesh->tangents)
							addVec3(mesh->tangents[existing_vertex], tangent, mesh->tangents[existing_vertex]);
					}
					return existing_vertex;
				}
			}
		}
		else
		{
			for (i = 0; i < mesh->vert_count; i++)
			{
				if (gmeshNearSameVertInline(mesh, i, pos, pos2, norm, norm2, binorm, tangent, tex1, tex2, color, varcolor, ignore_color_differences, ignore_color_alpha_differences, TANGENT_SPACE_TOLERANCE))
					return i;
			}
		}

	}

	if (mesh->grid.cell)
		polyGridFree(&mesh->grid);

	idx = mesh->vert_count;
	gmeshSetVertCount(mesh, mesh->vert_count + 1);
	if (mesh->positions && pos)
		copyVec3(pos, mesh->positions[idx]);
	if (mesh->positions2 && pos2)
		copyVec3(pos2, mesh->positions2[idx]);
	if (mesh->normals && norm)
        copyVec3(norm, mesh->normals[idx]);
	if (mesh->normals2 && norm2)
		copyVec3(norm2, mesh->normals2[idx]);
	if (mesh->binormals && binorm)
		copyVec3(binorm, mesh->binormals[idx]);
	if (mesh->tangents && tangent)
		copyVec3(tangent, mesh->tangents[idx]);
	if (mesh->tex1s && tex1)
		copyVec2(tex1, mesh->tex1s[idx]);
	if (mesh->tex2s && tex2)
		copyVec2(tex2, mesh->tex2s[idx]);
	if (mesh->boneweights && boneweight)
		copyVec4(boneweight, mesh->boneweights[idx]);
	if (mesh->bonemats && bonemat)
		memcpy(mesh->bonemats[idx], bonemat, sizeof(GBoneMats));
	if (mesh->colors && color)
		mesh->colors[idx] = *color;
	if (mesh->varcolors && varcolor)
		memcpy(&mesh->varcolors[idx*mesh->varcolor_size], varcolor, mesh->varcolor_size * sizeof(U8));
	if (check_exists && vhash && pos)
		addVertexToHashBucket(vhash, hash_pos, idx);
	return idx;
}

int gmeshAddVert(GMesh *mesh, const Vec3 pos, const Vec3 pos2, const Vec3 norm, const Vec3 norm2, 
				 const Vec3 binorm, const Vec3 tangent, 
				 const Vec2 tex1, const Vec2 tex2, 
				 const Color *color, const U8 *varcolor, 
				 const Vec4 boneweight, const GBoneMats bonemat, 
				 int check_exists, bool ignore_color_differences, bool ignore_color_alpha_differences)
{
	return gmeshAddVertInternal(mesh, pos, pos2, norm, norm2, binorm, tangent, tex1, tex2, color, varcolor, boneweight, bonemat, check_exists, NULL, mesh->vert_count, ignore_color_differences, ignore_color_alpha_differences);
}

int gmeshAddInterpolatedVert(GMesh *mesh, int idx1, int idx2, F32 ratio_1to2)
{
	F32 ratio_2to1 = 1.f - ratio_1to2;
	Vec3 pos, pos2, norm, norm2, binorm, tangent;
	Vec2 tex1, tex2;
	Color color;
	U8 *varcolor = NULL;

	if (mesh->positions)
	{
		scaleVec3(mesh->positions[idx2], ratio_1to2, pos);
		scaleAddVec3(mesh->positions[idx1], ratio_2to1, pos, pos);
	}
	if (mesh->positions2)
	{
		scaleVec3(mesh->positions2[idx2], ratio_1to2, pos2);
		scaleAddVec3(mesh->positions2[idx1], ratio_2to1, pos2, pos2);
	}
	if (mesh->normals)
	{
		scaleVec3(mesh->normals[idx2], ratio_1to2, norm);
		scaleAddVec3(mesh->normals[idx1], ratio_2to1, norm, norm);
	}
	if (mesh->normals2)
	{
		scaleVec3(mesh->normals2[idx2], ratio_1to2, norm2);
		scaleAddVec3(mesh->normals2[idx1], ratio_2to1, norm2, norm2);
	}
	if (mesh->binormals)
	{
		scaleVec3(mesh->binormals[idx2], ratio_1to2, binorm);
		scaleAddVec3(mesh->binormals[idx1], ratio_2to1, binorm, binorm);
	}
	if (mesh->tangents)
	{
		scaleVec3(mesh->tangents[idx2], ratio_1to2, tangent);
		scaleAddVec3(mesh->tangents[idx1], ratio_2to1, tangent, tangent);
	}
	if (mesh->tex1s)
	{
		scaleVec2(mesh->tex1s[idx2], ratio_1to2, tex1);
		scaleAddVec2(mesh->tex1s[idx1], ratio_2to1, tex1, tex1);
	}
	if (mesh->tex2s)
	{
		scaleVec2(mesh->tex2s[idx2], ratio_1to2, tex2);
		scaleAddVec2(mesh->tex2s[idx1], ratio_2to1, tex2, tex2);
	}
	if (mesh->colors)
	{
		Vec4 c1, c2, c3;
		colorToVec4(c1, mesh->colors[idx1]);
		colorToVec4(c2, mesh->colors[idx2]);
		scaleVec4(c2, ratio_1to2, c3);
		scaleAddVec4(c1, ratio_2to1, c3, c3);
		vec4ToColor(&color, c3);
	}
	if (mesh->varcolors)
	{
		int i;
		varcolor = _alloca(mesh->varcolor_size);
		for (i = 0; i < mesh->varcolor_size; ++i)
			varcolor[i] = (mesh->varcolors[idx1*mesh->varcolor_size + i] + mesh->varcolors[idx2*mesh->varcolor_size + i]) / 2;
	}

	return gmeshAddVert(mesh, 
		mesh->positions?pos:0, 
		mesh->positions2?pos2:0, 
		mesh->normals?norm:0, 
		mesh->normals2?norm2:0, 
		mesh->binormals?binorm:0, 
		mesh->tangents?tangent:0, 
		mesh->tex1s?tex1:0, 
		mesh->tex2s?tex2:0, 
		mesh->colors?&color:0, 
		mesh->varcolors?varcolor:0, 
		mesh->boneweights?mesh->boneweights[idx1]:0, 
		mesh->bonemats?mesh->bonemats[idx1]:0,
		0, false, false);
}

int gmeshAddTri(GMesh *mesh, int idx0, int idx1, int idx2, U16 tex_id, int check_exists)
{
	int i, idx;

	if (check_exists)
	{
		for (i = 0; i < mesh->tri_count; i++)
		{
			if (sameTri(mesh, i, idx0, idx1, idx2, tex_id))
				return i;
		}
	}

	if (mesh->grid.cell)
		polyGridFree(&mesh->grid);

	idx = mesh->tri_count;
	gmeshSetTriCount(mesh, mesh->tri_count + 1);
	mesh->tris[idx].idx[0] = idx0;
	mesh->tris[idx].idx[1] = idx1;
	mesh->tris[idx].idx[2] = idx2;
	mesh->tris[idx].tex_id = tex_id;
	return idx;
}

void gmeshRemoveTri(GMesh *mesh, int tri_idx)
{
	if (tri_idx >= mesh->tri_count || tri_idx < 0)
		return;

	if (mesh->grid.cell)
		polyGridFree(&mesh->grid);

	memcpy(mesh->tris + tri_idx, mesh->tris + tri_idx + 1, sizeof(*mesh->tris) * (mesh->tri_count - tri_idx - 1));
	mesh->tri_count--;
}

void gmeshMarkBadTri(GMesh *mesh, int tri_idx)
{
	mesh->tris[tri_idx].idx[0] = -1;
	mesh->tris[tri_idx].idx[1] = -1;
	mesh->tris[tri_idx].idx[2] = -1;
	mesh->tris[tri_idx].tex_id = -1;
}

void gmeshRemapVertex(GMesh *mesh, int old_idx, int new_idx)
{
	int i;

	if (old_idx >= mesh->vert_count || old_idx < 0 || new_idx >= mesh->vert_count || new_idx < 0)
		return;

	if (mesh->grid.cell)
		polyGridFree(&mesh->grid);

	for (i = 0; i < mesh->tri_count; i++)
	{
		GTriIdx *tri = &mesh->tris[i];
		if (tri->idx[0] == old_idx)
			tri->idx[0] = new_idx;
		if (tri->idx[1] == old_idx)
			tri->idx[1] = new_idx;
		if (tri->idx[2] == old_idx)
			tri->idx[2] = new_idx;
	}
}

void gmeshRemapTriVertex(GMesh *mesh, int tri_idx, int old_idx, int new_idx)
{
	GTriIdx *tri;

	if (old_idx >= mesh->vert_count || old_idx < 0 || new_idx >= mesh->vert_count || new_idx < 0)
		return;

	if (mesh->grid.cell)
		polyGridFree(&mesh->grid);

	tri = &mesh->tris[tri_idx];
	if (tri->idx[0] == old_idx)
		tri->idx[0] = new_idx;
	if (tri->idx[1] == old_idx)
		tri->idx[1] = new_idx;
	if (tri->idx[2] == old_idx)
		tri->idx[2] = new_idx;
}

void gmeshFreeData(GMesh *mesh)
{
	if (!mesh)
		return;
	SAFE_FREE(mesh->positions);
	SAFE_FREE(mesh->positions2);
	SAFE_FREE(mesh->normals);
	SAFE_FREE(mesh->normals2);
	SAFE_FREE(mesh->binormals);
	SAFE_FREE(mesh->tangents);
	SAFE_FREE(mesh->tex1s);
	SAFE_FREE(mesh->tex2s);
	SAFE_FREE(mesh->boneweights);
	SAFE_FREE(mesh->bonemats);
	SAFE_FREE(mesh->colors);
	SAFE_FREE(mesh->varcolors);
	SAFE_FREE(mesh->tris);
	if (mesh->grid.cell)
		polyGridFree(&mesh->grid);
	ZeroStruct(mesh);
}

void gmeshFree(GMesh *mesh)
{
	gmeshFreeData(mesh);
	free(mesh);
}

int gmeshGetMaterialCount(const GMesh *mesh)
{
	int i, max_material = 0;
	for (i = 0; i < mesh->tri_count; ++i)
		max_material = MAX(mesh->tris[i].tex_id, max_material);
	return max_material + 1;
}

F32 gmeshGetBounds(const GMesh *mesh, Vec3 local_min, Vec3 local_max, Vec3 local_mid)
{
	int i;

	if (!(mesh->usagebits & USE_POSITIONS) || !mesh->vert_count)
	{
		zeroVec3(local_min);
		zeroVec3(local_max);
		zeroVec3(local_mid);
		return 0;
	}

	setVec3same(local_min, 8e16);
	setVec3same(local_max, -8e16);

	for (i = 0; i < mesh->vert_count; ++i)
		vec3RunningMinMax(mesh->positions[i], local_min, local_max);

	centerVec3(local_min, local_max, local_mid);

	return distance3(local_mid, local_max);
}

void gmeshGetTriangleBarycentricCoordinates(const GMesh *mesh, int tri_idx, const Vec3 position, Vec3 barycentric_coords)
{
	if (tri_idx < 0 || tri_idx >= mesh->tri_count || !(mesh->usagebits * USE_POSITIONS))
	{
		zeroVec3(barycentric_coords);
		return;
	}

	{
		const GTriIdx *tri = &mesh->tris[tri_idx];
		const F32 *v0 = mesh->positions[tri->idx[0]];
		const F32 *v1 = mesh->positions[tri->idx[1]];
		const F32 *v2 = mesh->positions[tri->idx[2]];
		Vec3 p0, p1, p2, plane_normal;
		F32 a0, a1, a2;
		F32 total;

		subVec3(v1, v0, p0);
		subVec3(v2, v0, p1);
		crossVec3(p0, p1, plane_normal);
		total = 0.5f * lengthVec3(plane_normal);
		if (!total)
		{
			zeroVec3(barycentric_coords);
			return;
		}

		subVec3(v0, position, p0);
		subVec3(v1, position, p1);
		subVec3(v2, position, p2);

		crossVec3(p1, p2, plane_normal);
		a0 = 0.5f * lengthVec3(plane_normal);

		crossVec3(p0, p2, plane_normal);
		a1 = 0.5f * lengthVec3(plane_normal);

		crossVec3(p0, p1, plane_normal);
		a2 = 0.5f * lengthVec3(plane_normal);

		total = 1.f / total;
		barycentric_coords[0] = a0 * total;
		barycentric_coords[1] = a1 * total;
		barycentric_coords[2] = a2 * total;

		total = barycentric_coords[0] + barycentric_coords[1] + barycentric_coords[2];
		total = 1.f / total;
		scaleVec3(barycentric_coords, total, barycentric_coords);
	}
}

void gmeshTransform(GMesh *mesh, const Mat4 mat)
{
	int i;
	Vec3 v;

	if (!mesh || !mat)
		return;

	if (mesh->grid.cell)
		polyGridFree(&mesh->grid);
	
	if (mesh->positions)
	{
		for (i = 0; i < mesh->vert_count; ++i)
		{
			mulVecMat4(mesh->positions[i], mat, v);
			copyVec3(v, mesh->positions[i]);
		}
	}

	if (mesh->positions2)
	{
		for (i = 0; i < mesh->vert_count; ++i)
		{
			mulVecMat4(mesh->positions2[i], mat, v);
			copyVec3(v, mesh->positions2[i]);
		}
	}

	if (mesh->normals)
	{
		for (i = 0; i < mesh->vert_count; ++i)
		{
			mulVecMat3(mesh->normals[i], mat, v);
			normalVec3(v);
			copyVec3(v, mesh->normals[i]);
		}
	}

	if (mesh->normals2)
	{
		for (i = 0; i < mesh->vert_count; ++i)
		{
			mulVecMat3(mesh->normals2[i], mat, v);
			normalVec3(v);
			copyVec3(v, mesh->normals2[i]);
		}
	}

	if (mesh->binormals)
	{
		for (i = 0; i < mesh->vert_count; ++i)
		{
			mulVecMat3(mesh->binormals[i], mat, v);
			normalVec3(v);
			copyVec3(v, mesh->binormals[i]);
		}
	}

	if (mesh->tangents)
	{
		for (i = 0; i < mesh->vert_count; ++i)
		{
			mulVecMat3(mesh->tangents[i], mat, v);
			normalVec3(v);
			copyVec3(v, mesh->tangents[i]);
		}
	}
}

int gmeshAddTangentSpaceInternal_dbg(GMesh *mesh, bool flip_binormal MEM_DBG_PARMS)
{
	TangentSpaceGenData data={0};
	int anyBadBasis = 0;

	if (!(mesh->usagebits & (USE_POSITIONS|USE_NORMALS|USE_TEX1S)))
		return 0;

	PERFINFO_AUTO_START_FUNC();

	gmeshSetUsageBits_dbg(mesh, (mesh->usagebits | USE_BINORMALS | USE_TANGENTS) MEM_DBG_PARMS_CALL);

	data.tri_count = mesh->tri_count;
	data.tris = mesh->tris;

	data.vert_count = mesh->vert_count;
	data.positions = &(mesh->positions[0][0]);
	data.normals = &(mesh->normals[0][0]);
	data.binormals = &(mesh->binormals[0][0]);
	data.tangents = &(mesh->tangents[0][0]);
	data.texcoords = &(mesh->tex1s[0][0]);
	if (mesh->usagebits & USE_BONEWEIGHTS)
	{
		data.float_bone_weights = &(mesh->boneweights[0][0]);
		data.bone_ids = &(mesh->bonemats[0][0]);
	}
	if (mesh->usagebits & USE_COLORS)
		data.colors = mesh->colors;

	data.unwelded_tangent_basis = mesh->merge_average_tangent_basis;
	anyBadBasis = addTangentSpace(&data, flip_binormal);

	PERFINFO_AUTO_STOP();

	return anyBadBasis;
}

int gmeshAddTangentSpace_dbg(GMesh *mesh, bool use_wind, const char * asset_filename, enumLogCategory log_destination, bool flip_binormal MEM_DBG_PARMS)
{
	int fix_failed = 0;
	if (gmeshAddTangentSpaceInternal_dbg(mesh, flip_binormal MEM_DBG_PARMS_CALL))
	{
		// bad tangent space found during calculations, so unweld the vertices
		// and try again
		int initialVertexCount = mesh->vert_count;

		gmeshSetMergeAverageTangentBasis(mesh, true);
		gmeshUnpool_dbg(mesh, false, true, true MEM_DBG_PARMS_INIT);
		fix_failed = gmeshAddTangentSpaceInternal_dbg(mesh, flip_binormal MEM_DBG_PARMS_CALL);
		gmeshSetUsageBits(mesh, mesh->usagebits | USE_NORMALS);
		gmeshPool(mesh, true, use_wind, true);
		gmeshSetMergeAverageTangentBasis(mesh, false);

		if (log_destination != LOG_LAST)
		{
			// Model, Fix failed, Initial vertex count, Fixed vertex count, Extra verts, Extra bytes");
			log_printf(log_destination, "Bad tangent basis: %s, %d, %d, %d, %d, %d\n", 
				asset_filename,
				fix_failed, 
				initialVertexCount, 
				mesh->vert_count, 
				mesh->vert_count - initialVertexCount,
				(mesh->vert_count - initialVertexCount) * gmeshGetVertexSize(mesh));
		}
	}

	return fix_failed;
}

bool validateGConvexHull(const GConvexHull *hull)
{
	// hull is non-empty and not ill-formed, where ill-formed means non-zero plane count but NULL planes
	return !!hull->count == !!hull->planes;
}

GConvexHull *gmeshToGConvexHullEx(GMesh *mesh, int **tri_to_plane)
{
	GConvexHull *hull = calloc(1, sizeof(*hull));
	Vec3 a, b;
	Vec4 plane;
	int i;

	if (tri_to_plane)
		eaiSetSize(tri_to_plane, mesh->tri_count);

	for (i = 0; i < mesh->tri_count; i++)
	{
		if (tri_to_plane)
			(*tri_to_plane)[i] = -1;

		// skip collinear tris which form lines
		subVec3(mesh->positions[mesh->tris[i].idx[1]], mesh->positions[mesh->tris[i].idx[0]], a);
		subVec3(mesh->positions[mesh->tris[i].idx[2]], mesh->positions[mesh->tris[i].idx[0]], b);
		normalVec3(a);
		normalVec3(b);
		if (1.0f - fabs(dotVec3(a, b)) <= 0.001)
			continue;

		if (tri_to_plane)
			(*tri_to_plane)[i] = hull->count;
		makePlane(mesh->positions[mesh->tris[i].idx[0]], mesh->positions[mesh->tris[i].idx[1]], mesh->positions[mesh->tris[i].idx[2]], plane);
		hullAddPlane(hull, plane);
	}

	assert(validateGConvexHull(hull));

	return hull;
}

void gmeshCopy_dbg(GMesh *dst, const GMesh *src, int fill_grid MEM_DBG_PARMS)
{
	gmeshFreeData(dst);

	if (!dst->caller_fname)
	{
		MEM_DBG_STRUCT_PARMS_INIT(dst);
	}

	gmeshSetUsageBits_dbg(dst, src->usagebits MEM_DBG_PARMS_CALL);
	gmeshSetTriCount_dbg(dst, src->tri_count MEM_DBG_PARMS_CALL);
	gmeshSetVertCount_dbg(dst, src->vert_count MEM_DBG_PARMS_CALL);
	gmeshSetVarColorSize_dbg(dst, src->varcolor_size MEM_DBG_PARMS_CALL);
	if (src->positions)
		CopyStructs(dst->positions, src->positions, src->vert_count);
	if (src->positions2)
		CopyStructs(dst->positions2, src->positions2, src->vert_count);
	if (src->normals)
		CopyStructs(dst->normals, src->normals, src->vert_count);
	if (src->normals2)
		CopyStructs(dst->normals2, src->normals2, src->vert_count);
	if (src->binormals)
		CopyStructs(dst->binormals, src->binormals, src->vert_count);
	if (src->tangents)
		CopyStructs(dst->tangents, src->tangents, src->vert_count);
	if (src->tex1s)
		CopyStructs(dst->tex1s, src->tex1s, src->vert_count);
	if (src->tex2s)
		CopyStructs(dst->tex2s, src->tex2s, src->vert_count);
	if (src->boneweights)
		CopyStructs(dst->boneweights, src->boneweights, src->vert_count);
	if (src->bonemats)
		CopyStructs(dst->bonemats, src->bonemats, src->vert_count);
	if (src->colors)
		CopyStructs(dst->colors, src->colors, src->vert_count);
	if (src->varcolors)
		CopyStructs(dst->varcolors, src->varcolors, src->vert_count*src->varcolor_size);
	CopyStructs(dst->tris, src->tris, src->tri_count);
	if (fill_grid)
	{
		gmeshPool_dbg(dst, 0, false, false MEM_DBG_PARMS_CALL);
		polyGridFree(&dst->grid);
		gridPolys(&dst->grid, dst MEM_DBG_STRUCT_PARMS_CALL(dst));
	}
	dst->dbg_color = src->dbg_color;
}

// appends tris from src to dst
void gmeshMerge_dbg(GMesh *dst, const GMesh *src, int pool_verts, int pool_tris, bool ignore_color_differences, bool ignore_color_alpha_differences MEM_DBG_PARMS)
{
	int i, origcount = dst->vert_count;
	VertexHash vhash = {0};

	if (!dst->caller_fname)
	{
		MEM_DBG_STRUCT_PARMS_INIT(dst);
	}

	if (pool_verts && dst->positions)
	{
		for (i = 0; i < dst->vert_count; ++i)
		{
			IVec3 hash_pos;
			hashVertex(dst->positions[i], hash_pos);
			eaiPush(&vhash.vert_buckets[hash_pos[2]][hash_pos[1]][hash_pos[0]], i);
		}
	}

	gmeshSetUsageBits_dbg(dst, (dst->usagebits | src->usagebits) MEM_DBG_PARMS_CALL);
	gmeshSetVarColorSize_dbg(dst, src->varcolor_size MEM_DBG_PARMS_CALL);

	for (i = 0; i < src->tri_count; i++)
	{
		int idx0, idx1, idx2;
		GTriIdx *srctri = &src->tris[i];
		int i0 = srctri->idx[0], i1 = srctri->idx[1], i2 = srctri->idx[2];

		if (i0 < 0 || i1 < 0 || i2 < 0)
			continue;

		if (src->positions && isDegenerate(src->positions[i0], src->positions[i1], src->positions[i2]))
			continue;

		idx0 = gmeshAddVertInternal(dst,
			src->positions?src->positions[i0]:0,
			src->positions2?src->positions2[i0]:0,
			src->normals?src->normals[i0]:0,
			src->normals2?src->normals2[i0]:0,
			src->binormals?src->binormals[i0]:0,
			src->tangents?src->tangents[i0]:0,
			src->tex1s?src->tex1s[i0]:0,
			src->tex2s?src->tex2s[i0]:0,
			src->colors?&src->colors[i0]:0,
			src->varcolors?&src->varcolors[i0*src->varcolor_size]:0,
			src->boneweights?src->boneweights[i0]:0,
			src->bonemats?src->bonemats[i0]:0,
			pool_verts, &vhash, origcount, ignore_color_differences, ignore_color_alpha_differences);
		idx1 = gmeshAddVertInternal(dst,
			src->positions?src->positions[i1]:0,
			src->positions2?src->positions2[i1]:0,
			src->normals?src->normals[i1]:0,
			src->normals2?src->normals2[i1]:0,
			src->binormals?src->binormals[i1]:0,
			src->tangents?src->tangents[i1]:0,
			src->tex1s?src->tex1s[i1]:0,
			src->tex2s?src->tex2s[i1]:0,
			src->colors?&src->colors[i1]:0,
			src->varcolors?&src->varcolors[i1*src->varcolor_size]:0,
			src->boneweights?src->boneweights[i1]:0,
			src->bonemats?src->bonemats[i1]:0,
			pool_verts, &vhash, origcount, ignore_color_differences, ignore_color_alpha_differences);
		idx2 = gmeshAddVertInternal(dst,
			src->positions?src->positions[i2]:0,
			src->positions2?src->positions2[i2]:0,
			src->normals?src->normals[i2]:0,
			src->normals2?src->normals2[i2]:0,
			src->binormals?src->binormals[i2]:0,
			src->tangents?src->tangents[i2]:0,
			src->tex1s?src->tex1s[i2]:0,
			src->tex2s?src->tex2s[i2]:0,
			src->colors?&src->colors[i2]:0,
			src->varcolors?&src->varcolors[i2*src->varcolor_size]:0,
			src->boneweights?src->boneweights[i2]:0,
			src->bonemats?src->bonemats[i2]:0,
			pool_verts, &vhash, origcount, ignore_color_differences, ignore_color_alpha_differences);
		gmeshAddTri(dst, idx0, idx1, idx2, srctri->tex_id, pool_tris);
	}

	if (dst->merge_average_tangent_basis)
	{
		if (dst->normals)
		{
			for (i = 0; i < dst->vert_count; i++)
				normalVec3(dst->normals[i]);
		}
		if (dst->binormals)
		{
			for (i = 0; i < dst->vert_count; i++)
				normalVec3(dst->binormals[i]);
		}
		if (dst->tangents)
		{
			for (i = 0; i < dst->vert_count; i++)
				normalVec3(dst->tangents[i]);
		}
	}

	freeVertexHashData(&vhash);
}

void gmeshPool_dbg(GMesh *mesh, int pool_tris, bool ignore_color_differences, bool ignore_color_alpha_differences MEM_DBG_PARMS)
{
	GMesh	simple={0};

	if (!mesh->positions)
		return;

	simple.merge_average_tangent_basis = mesh->merge_average_tangent_basis;
	gmeshMerge_dbg(&simple, mesh, true, pool_tris, ignore_color_differences, ignore_color_alpha_differences MEM_DBG_PARMS_CALL);
	gmeshFreeData(mesh);
	*mesh = simple;
}

void gmeshUnpool_dbg(GMesh *mesh, int pool_tris, bool ignore_color_differences, bool ignore_color_alpha_differences MEM_DBG_PARMS)
{
	GMesh	simple={0};

	if (!mesh->positions)
		return;

	simple.merge_average_tangent_basis = mesh->merge_average_tangent_basis;
	gmeshMerge_dbg(&simple, mesh, false, pool_tris, ignore_color_differences, ignore_color_alpha_differences MEM_DBG_PARMS_CALL);
	gmeshFreeData(mesh);
	*mesh = simple;
}

int gmeshMarkDegenerateTris(GMesh *mesh)
{
	int i, found = 0;

	if (!mesh->positions)
		return 0;

	for (i = 0; i < mesh->tri_count; i++)
	{
		int i0, i1, i2;

		i0 = mesh->tris[i].idx[0];
		i1 = mesh->tris[i].idx[1];
		i2 = mesh->tris[i].idx[2];

		if (i0 < 0 || i1 < 0 || i2 < 0)
		{
			found++;
			continue;
		}

		if (isDegenerate(mesh->positions[i0], mesh->positions[i1], mesh->positions[i2]) && (!mesh->positions2 || isDegenerate(mesh->positions2[i0], mesh->positions2[i1], mesh->positions2[i2])))
		{
			gmeshMarkBadTri(mesh, i);
			found++;
		}
	}

	return found;
}

static int splitEdge(GMesh *mesh, int i0, int i1, int i_new)
{
	int i, new_tris = 0;

	for (i = mesh->tri_count - 1; i >= 0; --i)
	{
		GTriIdx *tri = &mesh->tris[i];

		if ((tri->idx[0] == i0 && tri->idx[1] == i1) || (tri->idx[1] == i0 && tri->idx[0] == i1))
		{
			gmeshAddTri(mesh, i_new, tri->idx[1], tri->idx[2], tri->tex_id, false);
			tri = &mesh->tris[i]; // gmeshAddTri could have reallocated the tris array
			tri->idx[1] = i_new;
			new_tris++;
		}
		else if ((tri->idx[1] == i0 && tri->idx[2] == i1) || (tri->idx[2] == i0 && tri->idx[1] == i1))
		{
			gmeshAddTri(mesh, tri->idx[0], i_new, tri->idx[2], tri->tex_id, false);
			tri = &mesh->tris[i]; // gmeshAddTri could have reallocated the tris array
			tri->idx[2] = i_new;
			new_tris++;
		}
		else if ((tri->idx[2] == i0 && tri->idx[0] == i1) || (tri->idx[0] == i0 && tri->idx[2] == i1))
		{
			gmeshAddTri(mesh, i_new, tri->idx[1], tri->idx[2], tri->tex_id, false);
			tri = &mesh->tris[i]; // gmeshAddTri could have reallocated the tris array
			tri->idx[2] = i_new;
			new_tris++;
		}
	}

	return new_tris;
}

static int split(GMesh *mesh, int src_tri_idx, int i0, int i1, int i2, const Vec4 plane)
{
	// point i0 is on the plane
	float pval1, pval2, ratio_12;
	int i3, tri_idx1, new_tris = 0;
#if DEBUG_SPLIT
	Vec3 tri_normal, tri_normal1, tri_normal2;
#endif

	pval1 = distanceToPlane(mesh->positions[i1], plane);
	pval2 = distanceToPlane(mesh->positions[i2], plane);

	if(pval1 - pval2 == 0)
		return 0;

	ratio_12 = pval1 / (pval1 - pval2);

	if (ratio_12 < EPSILON || ratio_12 > (1 - EPSILON))
		return 0;

#if DEBUG_SPLIT
	makePlaneNormal(mesh->positions[i0], mesh->positions[i1], mesh->positions[i2], tri_normal);
#endif

	CHECKHEAP;
	i3 = gmeshAddInterpolatedVert(mesh, i1, i2, ratio_12);
	CHECKHEAP;


	// make this tri on the front of the plane
	if (pval1 > 0)
	{
		mesh->tris[src_tri_idx].idx[0] = i0;
		mesh->tris[src_tri_idx].idx[1] = i1;
		mesh->tris[src_tri_idx].idx[2] = i3;

		tri_idx1 = gmeshAddTri(mesh, i0, i3, i2, mesh->tris[src_tri_idx].tex_id, false);
		new_tris++;
	}
	else
	{
		mesh->tris[src_tri_idx].idx[0] = i0;
		mesh->tris[src_tri_idx].idx[1] = i3;
		mesh->tris[src_tri_idx].idx[2] = i2;

		tri_idx1 = gmeshAddTri(mesh, i0, i1, i3, mesh->tris[src_tri_idx].tex_id, false);
		new_tris++;
	}
	CHECKHEAP;

	new_tris += splitEdge(mesh, i1, i2, i3);

#if DEBUG_SPLIT
	makePlaneNormal(mesh->positions[mesh->tris[src_tri_idx].idx[0]],
					mesh->positions[mesh->tris[src_tri_idx].idx[1]],
					mesh->positions[mesh->tris[src_tri_idx].idx[2]],
					tri_normal1);
	makePlaneNormal(mesh->positions[mesh->tris[tri_idx1].idx[0]],
					mesh->positions[mesh->tris[tri_idx1].idx[1]],
					mesh->positions[mesh->tris[tri_idx1].idx[2]],
					tri_normal2);
	assert(dotVec3(tri_normal, tri_normal1) > 0.75f);
	assert(dotVec3(tri_normal, tri_normal2) > 0.75f);
#endif

	return new_tris;
}

static int split2(GMesh *mesh, int src_tri_idx, int i0, int i1, int i2, const Vec4 plane)
{
	// point i0 is alone on one side of the plane
	float pval0, pval1, pval2, ratio_01, ratio_02;
	int i3, i4, tri_idx1, tri_idx2, new_tris = 0;
#if DEBUG_SPLIT
	Vec3 tri_normal, tri_normal1, tri_normal2, tri_normal3;
#endif

	pval0 = distanceToPlane(mesh->positions[i0], plane);
	pval1 = distanceToPlane(mesh->positions[i1], plane);
	pval2 = distanceToPlane(mesh->positions[i2], plane);

	if(pval0 - pval1 == 0 || pval0 - pval2 == 0)
		return 0;

	ratio_01 = pval0 / (pval0 - pval1);
	ratio_02 = pval0 / (pval0 - pval2);

	if (ratio_01 < EPSILON || ratio_01 > (1 - EPSILON) || ratio_02 < EPSILON || ratio_02 > (1 - EPSILON))
		return 0;

#if DEBUG_SPLIT
	makePlaneNormal(mesh->positions[i0], mesh->positions[i1], mesh->positions[i2], tri_normal);
#endif

	CHECKHEAP;
	i3 = gmeshAddInterpolatedVert(mesh, i0, i1, ratio_01);
	i4 = gmeshAddInterpolatedVert(mesh, i0, i2, ratio_02);
	CHECKHEAP;

	// make this tri on the front of the plane
	if (pval0 > 0)
	{
		mesh->tris[src_tri_idx].idx[0] = i0;
		mesh->tris[src_tri_idx].idx[1] = i3;
		mesh->tris[src_tri_idx].idx[2] = i4;

		tri_idx1 = gmeshAddTri(mesh, i3, i1, i4, mesh->tris[src_tri_idx].tex_id, false);

		tri_idx2 = gmeshAddTri(mesh, i1, i2, i4, mesh->tris[src_tri_idx].tex_id, false);
		new_tris += 2;
	}
	else
	{
		tri_idx1 = gmeshAddTri(mesh, i0, i3, i4, mesh->tris[src_tri_idx].tex_id, false);

		mesh->tris[src_tri_idx].idx[0] = i3;
		mesh->tris[src_tri_idx].idx[1] = i1;
		mesh->tris[src_tri_idx].idx[2] = i4;

		tri_idx2 = gmeshAddTri(mesh, i1, i2, i4, mesh->tris[src_tri_idx].tex_id, false);
		new_tris += 2;
	}
	CHECKHEAP;

	new_tris += splitEdge(mesh, i0, i1, i3);
	new_tris += splitEdge(mesh, i0, i2, i4);

#if DEBUG_SPLIT
	makePlaneNormal(mesh->positions[mesh->tris[src_tri_idx].idx[0]],
					mesh->positions[mesh->tris[src_tri_idx].idx[1]],
					mesh->positions[mesh->tris[src_tri_idx].idx[2]],
					tri_normal1);
	makePlaneNormal(mesh->positions[mesh->tris[tri_idx1].idx[0]],
					mesh->positions[mesh->tris[tri_idx1].idx[1]],
					mesh->positions[mesh->tris[tri_idx1].idx[2]],
					tri_normal2);
	makePlaneNormal(mesh->positions[mesh->tris[tri_idx2].idx[0]],
					mesh->positions[mesh->tris[tri_idx2].idx[1]],
					mesh->positions[mesh->tris[tri_idx2].idx[2]],
					tri_normal3);
	assert(dotVec3(tri_normal, tri_normal1) > 0.75f);
	assert(dotVec3(tri_normal, tri_normal2) > 0.75f);
	assert(dotVec3(tri_normal, tri_normal3) > 0.75f);
#endif

	return new_tris;
}

__forceinline static float distanceToPlaneEpsilon(const Vec3 pos, const Vec4 plane, float epsilon)
{
	float dist = dotVec3(pos, plane) - plane[3];
	dist = FloatBranchFL(fabsf(dist), epsilon * fabs(plane[3]), 0.0f, dist);
	return dist;
}


// returns 0 for behind plane, 1 for on plane, 2 for in front of plane
__forceinline static int classifyPointEpsilon(const Vec3 pos, const Vec4 plane, float epsilon)
{
	float dist = distanceToPlaneEpsilon(pos, plane, epsilon);
	if (dist > 0)
		return 2;
	return dist == 0;
}

// returns number of new triangles created
int gmeshSplitTri(GMesh *mesh, const Vec4 plane, int tri_idx)
{
	int v0, v1, v2, c0, c1, c2, total, new_tris = 0;

	if (tri_idx >= mesh->tri_count || tri_idx < 0)
		return 0;

	v0 = mesh->tris[tri_idx].idx[0];
	v1 = mesh->tris[tri_idx].idx[1];
	v2 = mesh->tris[tri_idx].idx[2];
	c0 = classifyPointEpsilon(mesh->positions[v0], plane, FLT_EPSILON * 1000);
	c1 = classifyPointEpsilon(mesh->positions[v1], plane, FLT_EPSILON * 1000);
	c2 = classifyPointEpsilon(mesh->positions[v2], plane, FLT_EPSILON * 1000);

	// check if all on same side of plane
	if (c0 == c1 && c1 == c2)
		return 0;

	total = c0 + c1 + c2;

	if (c0 == 1 || c1 == 1 || c2 == 1)
	{
		// order does not matter
		switch (total)
		{
		case 1: // 0, 0, 1
		case 2: // 0, 1, 1
		case 4: // 1, 1, 2
		case 5: // 1, 2, 2
			return 0;
		}

		// 0, 1, 2
		// we need one new triangle

		// find the on plane point:
		if (c0 == 1)
			new_tris += split(mesh, tri_idx, v0, v1, v2, plane);
		else if (c1 == 1)
			new_tris += split(mesh, tri_idx, v1, v2, v0, plane);
		else
			new_tris += split(mesh, tri_idx, v2, v0, v1, plane);
	}
	else
	{
		// 0, 0, 2
		// 0, 2, 2
		// we need two new triangles

		if (total == 2)
		{
			// find the on plane side point
			if (c0 == 2)
				new_tris += split2(mesh, tri_idx, v0, v1, v2, plane);
			else if (c1 == 2)
				new_tris += split2(mesh, tri_idx, v1, v2, v0, plane);
			else
				new_tris += split2(mesh, tri_idx, v2, v0, v1, plane);
		}
		else // total == 4
		{
			// find the off plane side point
			if (c0 == 0)
				new_tris += split2(mesh, tri_idx, v0, v1, v2, plane);
			else if (c1 == 0)
				new_tris += split2(mesh, tri_idx, v1, v2, v0, plane);
			else
				new_tris += split2(mesh, tri_idx, v2, v0, v1, plane);
		}
	}

	return new_tris;
}

int gmeshSplitTriCoplanar(GMesh *mesh, const Vec3 p1, const Vec3 p2, const Vec3 p3, int tri_idx)
{
	Vec4 plane;
	Vec3 tmp_p;
	Vec3 tri_norm;
	int tris_added = 0;

	makePlaneNormal(p1, p2, p3, tri_norm);
	
	addVec3(tri_norm, p1, tmp_p);
	makePlane(p1, tmp_p, p2, plane);
	tris_added += gmeshSplitTri(mesh, plane, tri_idx);	

	addVec3(tri_norm, p2, tmp_p);
	makePlane(p2, tmp_p, p3, plane);
	tris_added += gmeshSplitTri(mesh, plane, tri_idx);	

	addVec3(tri_norm, p3, tmp_p);
	makePlane(p3, tmp_p, p1, plane);
	tris_added += gmeshSplitTri(mesh, plane, tri_idx);	

	return tris_added;
}

typedef struct TriVertsAndIdx {
	unsigned int idx;
	unsigned int color;
} TriVertsAndIdx;

static int triVColorCmp(const TriVertsAndIdx *tri1, const TriVertsAndIdx *tri2)
{
	return tri1->color - tri2->color;
}

void gmeshSortTrisByVertColor(GMesh *mesh)
{
	TriVertsAndIdx *triCmpHolder = ScratchAlloc(sizeof(TriVertsAndIdx) * mesh->tri_count);
	GTriIdx *tempTris = ScratchAlloc(sizeof(GTriIdx) * mesh->tri_count);
	int i = 0;

	if (!(mesh->usagebits & USE_COLORS))
		return;
	while (i < mesh->tri_count) {
		U16 current_tex_idx = mesh->tris[i].tex_id;
		int j = 0;
		// Sort tris within sub-objects, not across the entire object.
		while (((i + j) < mesh->tri_count) && mesh->tris[i+j].tex_id == current_tex_idx) {
			int k;
			int currentTriIdx = i+j;
			triCmpHolder[currentTriIdx].idx = currentTriIdx;
			for (k = 0; k < 3; k++)
				triCmpHolder[currentTriIdx].color += mesh->colors[mesh->tris[currentTriIdx].idx[k]].r;	// sorting object according to the total of the red channel for all associated verts for the triangle
			j++;
		}
		qsort(&triCmpHolder[i],j,sizeof(TriVertsAndIdx),triVColorCmp);
		i += j;
	}

	assert(i == mesh->tri_count);

	for (i = 0; i < mesh->tri_count; i++)
		tempTris[i] = mesh->tris[triCmpHolder[i].idx];

	memcpy(mesh->tris, tempTris, sizeof(GTriIdx) * mesh->tri_count);
	ScratchFree(triCmpHolder);
	ScratchFree(tempTris);
	gmeshSetUsageBits(mesh, mesh->usagebits ^ USE_COLORS);	// tris have been sorted, wipe colors before they are improperly used.

	return;
}

static int tricmp(const int *idx1, const int *idx2)
{
	return *idx1 - *idx2;
}

int gmeshSortTrisByTexID(GMesh *mesh, int (*cmp) (int *, int *))
{
	int		*temp_ids,tc=0,i,j,tricount=0;
	GTriIdx	*temp_tris;

	temp_tris = ScratchAlloc(sizeof(GTriIdx) * mesh->tri_count);
	temp_ids = ScratchAlloc(sizeof(int) * mesh->tri_count);
	for(i=0;i<mesh->tri_count;i++)
	{
		for(j=0;j<tc;j++)
		{
			if (mesh->tris[i].tex_id == temp_ids[j])
				break;
		}
		if (j >= tc)
			temp_ids[tc++] = mesh->tris[i].tex_id;
	}

	if (!cmp)
		cmp = tricmp;

	qsort(temp_ids,tc,sizeof(int),(int (*) (const void *, const void *))cmp);

	for(i=0;i<tc;i++)
	{
		for(j=0;j<mesh->tri_count;j++)
		{
			if (temp_ids[i] == mesh->tris[j].tex_id)
			{
				temp_tris[tricount++] = mesh->tris[j];
			}
		}
	}
	assert(tricount == mesh->tri_count);

	memcpy(mesh->tris,temp_tris,sizeof(GTriIdx) * mesh->tri_count);
	ScratchFree(temp_ids);
	ScratchFree(temp_tris);

	return tc;
}

static int cmpGTriIdx(const GTriIdx *tri1, const GTriIdx *tri2)
{
	int t;

	t = tri1->idx[0] - tri2->idx[0];
	if (t)
		return t;

	t = tri1->idx[1] - tri2->idx[1];
	if (t)
		return t;

	t = tri1->idx[2] - tri2->idx[2];
	if (t)
		return t;

	return tri1->tex_id - tri2->tex_id;
}

typedef struct MeshSort
{
	GMesh *mesh;
	int orig_idx;
} MeshSort;

static int cmpMeshSort(const MeshSort *sort1, const MeshSort *sort2)
{
	GMesh *mesh = sort1->mesh;

#define CMP_VEC2(field) if (mesh->field) { int t = cmpVec2(mesh->field[sort1->orig_idx], mesh->field[sort2->orig_idx]); if (t) return t; }
#define CMP_VEC3(field) if (mesh->field) { int t = cmpVec3XZY(mesh->field[sort1->orig_idx], mesh->field[sort2->orig_idx]); if (t) return t; }
#define CMP_VEC4(field) if (mesh->field) { int t = cmpVec4(mesh->field[sort1->orig_idx], mesh->field[sort2->orig_idx]); if (t) return t; }
#define CMP_IVEC4(field) if (mesh->field) { int t = cmpIVec4(mesh->field[sort1->orig_idx], mesh->field[sort2->orig_idx]); if (t) return t; }

	CMP_VEC3(positions);
	CMP_VEC3(positions2);
	CMP_VEC3(normals);
	CMP_VEC3(normals2);
	CMP_VEC3(binormals);
	CMP_VEC3(tangents);
	CMP_VEC2(tex1s);
	CMP_VEC2(tex2s);
	CMP_VEC4(boneweights);

	 if (mesh->bonemats)
	 {
		 CMP_IVEC_COMPONENT(mesh->bonemats[sort1->orig_idx], mesh->bonemats[sort2->orig_idx], 0);
		 CMP_IVEC_COMPONENT(mesh->bonemats[sort1->orig_idx], mesh->bonemats[sort2->orig_idx], 1);
		 CMP_IVEC_COMPONENT(mesh->bonemats[sort1->orig_idx], mesh->bonemats[sort2->orig_idx], 2);
		 CMP_IVEC_COMPONENT(mesh->bonemats[sort1->orig_idx], mesh->bonemats[sort2->orig_idx], 3);
	 }

	 if (mesh->colors)
	 {
		 CMP_IVEC_COMPONENT(mesh->colors[sort1->orig_idx].rgba, mesh->colors[sort2->orig_idx].rgba, 0);
		 CMP_IVEC_COMPONENT(mesh->colors[sort1->orig_idx].rgba, mesh->colors[sort2->orig_idx].rgba, 1);
		 CMP_IVEC_COMPONENT(mesh->colors[sort1->orig_idx].rgba, mesh->colors[sort2->orig_idx].rgba, 2);
		 CMP_IVEC_COMPONENT(mesh->colors[sort1->orig_idx].rgba, mesh->colors[sort2->orig_idx].rgba, 3);
	 }

	 if (mesh->varcolors)
	 {
		 int i;
		 for (i = 0; i < mesh->varcolor_size; ++i)
			 CMP_IVEC_COMPONENT(&mesh->varcolors[sort1->orig_idx*mesh->varcolor_size], &mesh->varcolors[sort2->orig_idx*mesh->varcolor_size], i);
	 }

	 return 0;
}

#define ArrayRemoveElement(pArray, iIdx, iArrayCount) if (pArray) { memmove(&(pArray)[(iIdx)], &(pArray)[(iIdx)+1], ((iArrayCount) - (iIdx) - 1) * sizeof((pArray)[0])); }

void gmeshSort(GMesh *mesh)
{
	int i, j;

	if (mesh->positions)
	{
		U8 *temp_varcolor = NULL;
		MeshSort *sort_nodes;
		int *vertremap, *trivertremap, *deletedverts, deletedvertcount = 0;

		if (mesh->grid.cell)
			polyGridFree(&mesh->grid);

		// reorder verts
		sort_nodes = ScratchAlloc(mesh->vert_count * sizeof(MeshSort));
		for (i = 0; i < mesh->vert_count; ++i)
		{
			sort_nodes[i].mesh = mesh;
			sort_nodes[i].orig_idx = i;
		}

		// sort by position
		qsort(sort_nodes, mesh->vert_count, sizeof(MeshSort), cmpMeshSort);

		vertremap = ScratchAlloc(mesh->vert_count * sizeof(int));
		trivertremap = ScratchAlloc(mesh->vert_count * sizeof(int));
		deletedverts = ScratchAlloc(mesh->vert_count * sizeof(int));

		for (i = 0; i < mesh->vert_count; ++i)
		{
			vertremap[sort_nodes[i].orig_idx] = i;
			trivertremap[sort_nodes[i].orig_idx] = i;
			for (j = i-1; j >= 0; --j)
			{
				if (cmpMeshSort(&sort_nodes[i], &sort_nodes[j]) != 0)
					break;
				trivertremap[sort_nodes[i].orig_idx] = j;
			}
			if (trivertremap[sort_nodes[i].orig_idx] != i)
				deletedverts[deletedvertcount++] = i;
		}

		for (i = 0; i < mesh->tri_count; ++i)
		{
			mesh->tris[i].idx[0] = trivertremap[mesh->tris[i].idx[0]];
			mesh->tris[i].idx[1] = trivertremap[mesh->tris[i].idx[1]];
			mesh->tris[i].idx[2] = trivertremap[mesh->tris[i].idx[2]];
		}

		if (mesh->varcolors)
			temp_varcolor = _alloca(mesh->varcolor_size * sizeof(U8));

		for (i = 0; i < mesh->vert_count; ++i)
		{
			while (vertremap[i] != i)
			{
				int dest = vertremap[i];

				SWAPVEC3(mesh->positions[i], mesh->positions[dest]);

				if (mesh->positions2)
					SWAPVEC3(mesh->positions2[i], mesh->positions2[dest]);
				if (mesh->normals)
					SWAPVEC3(mesh->normals[i], mesh->normals[dest]);
				if (mesh->normals2)
					SWAPVEC3(mesh->normals2[i], mesh->normals2[dest]);
				if (mesh->binormals)
					SWAPVEC3(mesh->binormals[i], mesh->binormals[dest]);
				if (mesh->tangents)
					SWAPVEC3(mesh->tangents[i], mesh->tangents[dest]);
				if (mesh->tex1s)
					SWAPVEC2(mesh->tex1s[i], mesh->tex1s[dest]);
				if (mesh->tex2s)
					SWAPVEC2(mesh->tex2s[i], mesh->tex2s[dest]);
				if (mesh->boneweights)
					SWAPVEC4(mesh->boneweights[i], mesh->boneweights[dest]);
				if (mesh->bonemats)
					SWAPIVEC4(mesh->bonemats[i], mesh->bonemats[dest]);
				if (mesh->colors)
					SWAP32(mesh->colors[i].integer_for_equality_only, mesh->colors[dest].integer_for_equality_only);
				if (mesh->varcolors)
				{
					memcpy(temp_varcolor, &mesh->varcolors[i*mesh->varcolor_size], mesh->varcolor_size * sizeof(U8));
					memcpy(&mesh->varcolors[i*mesh->varcolor_size], &mesh->varcolors[dest*mesh->varcolor_size], mesh->varcolor_size * sizeof(U8));
					memcpy(&mesh->varcolors[dest*mesh->varcolor_size], temp_varcolor, mesh->varcolor_size * sizeof(U8));
				}

				SWAP32(vertremap[i], vertremap[dest]);
			}
		}

		// remove deleted verts
		for (i = deletedvertcount-1; i >= 0; --i)
		{
			int dvidx = deletedverts[i];

			if (dvidx < mesh->vert_count - 1)
			{
				ArrayRemoveElement(mesh->positions, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->positions2, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->normals, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->normals2, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->binormals, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->tangents, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->tex1s, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->tex2s, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->boneweights, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->bonemats, dvidx, mesh->vert_count);
				ArrayRemoveElement(mesh->colors, dvidx, mesh->vert_count);
				if (mesh->varcolors)
					memmove(&mesh->varcolors[dvidx * mesh->varcolor_size], &mesh->varcolors[(dvidx+1) * mesh->varcolor_size], (mesh->vert_count - dvidx - 1) * sizeof(U8) * mesh->varcolor_size);

				for (j = 0; j < mesh->tri_count; ++j)
				{
					if (mesh->tris[j].idx[0] >= dvidx)
						mesh->tris[j].idx[0]--;
					if (mesh->tris[j].idx[1] >= dvidx)
						mesh->tris[j].idx[1]--;
					if (mesh->tris[j].idx[2] >= dvidx)
						mesh->tris[j].idx[2]--;
				}
			}

			mesh->vert_count--;
		}

		for (i = 0; i < mesh->tri_count; ++i)
		{
			int lowest_idx = 0;
			if (mesh->tris[i].idx[1] < mesh->tris[i].idx[lowest_idx])
				lowest_idx = 1;
			if (mesh->tris[i].idx[2] < mesh->tris[i].idx[lowest_idx])
				lowest_idx = 2;
			for (j = 0; j < lowest_idx; ++j)
				leftShiftIVec3(mesh->tris[i].idx);
		}

		ScratchFree(deletedverts);
		ScratchFree(trivertremap);
		ScratchFree(vertremap);
		ScratchFree(sort_nodes);
	}

	// reorder tris
	qsort(mesh->tris, mesh->tri_count, sizeof(GTriIdx), cmpGTriIdx);
}

int addTangentSpace(TangentSpaceGenData *data, bool flip_binormal)
{
	Vec3				*tangents,*binormals;
	Mat3				basis;
	int					i,j,idx;
	int					*redirects;
	int					use_fbw = !!data->float_bone_weights;
	int					use_bwt = !!data->bone_weights;
	int					use_bid = !!data->bone_ids;
	int					use_clr = !!data->colors;
	int					foundBadBasis = 0;

#define VPOS(i) ((F32 *)(((U8 *)data->positions) + data->position_stride * (i)))
#define VNRM(i) ((F32 *)(((U8 *)data->normals) + data->normal_stride * (i)))
#define VTEX(i) ((F32 *)(((U8 *)data->texcoords) + data->texcoord_stride * (i)))
#define VBWT(i) (*((U32 *)(((U32 *)data->bone_weights) + data->bone_weight_stride * (i))))
#define VFBW(i) ((F32 *)(((U8 *)data->float_bone_weights) + data->float_bone_weight_stride * (i)))
#define VBID(i) ((U16 *)(((U8 *)data->bone_ids) + data->bone_id_stride * (i)))
#define VCLR(i) ((Color *)(((U8 *)data->colors) + data->color_stride * (i)))
#define VTANGENT(i) ((F32 *)(((U8 *)data->tangents) + data->tangent_stride * (i)))
#define VBINORMAL(i) ((F32 *)(((U8 *)data->binormals) + data->binormal_stride * (i)))

	if (!data->positions || !data->normals || !data->texcoords || !data->binormals || !data->tangents || !data->vert_count)
		return 0;

	PERFINFO_AUTO_START(__FUNCTION__,1);

	tangents = ScratchAlloc(data->vert_count * sizeof(Vec3));
	binormals = ScratchAlloc(data->vert_count * sizeof(Vec3));
	redirects = ScratchAlloc(data->vert_count * sizeof(int));
	assert(tangents && binormals && redirects);

	if (!data->position_stride)
		data->position_stride = sizeof(Vec3);
	if (!data->normal_stride)
		data->normal_stride = sizeof(Vec3);
	if (!data->texcoord_stride)
		data->texcoord_stride = sizeof(Vec2);
	if (!data->bone_weight_stride)
		data->bone_weight_stride = sizeof(Vec4);
	if (!data->bone_id_stride)
		data->bone_id_stride = sizeof(U16) * 4;
	if (!data->binormal_stride)
		data->binormal_stride = sizeof(Vec3);
	if (!data->tangent_stride)
		data->tangent_stride = sizeof(Vec3);

	for (i = 0; i < data->vert_count; i++)
	{
		if (data->unwelded_tangent_basis)
			j = i;
		else
		{
			for (j = 0; j < i; j++)
			{
				if (sameVec3(VPOS(i), VPOS(j)) && 
					sameVec3(VNRM(i), VNRM(j)) && 
					sameVec2(VTEX(i), VTEX(j)) && 
					(!use_bwt || VBWT(i) == VBWT(j)) &&
					(!use_fbw || sameVec4(VFBW(i), VFBW(j))) &&
					(!use_bid || sameVec4(VBID(i), VBID(j))) &&
					(!use_clr || VCLR(i)->integer_for_equality_only == VCLR(j)->integer_for_equality_only))
					break;
			}
		}
		redirects[i] = j;
	}

	for (i=0;i<data->tri_count;i++)
	{
		int		v0_idx,v1_idx,v2_idx;
		F32		*n0,*n1,*n2,*v0,*v1,*v2,*t0,*t1,*t2,*n;

		if (data->tris)
		{
			v0_idx = data->tris[i].idx[0];
			v1_idx = data->tris[i].idx[1];
			v2_idx = data->tris[i].idx[2];
		}
		else
		{
			v0_idx = data->tri_indices[i*3 + 0];
			v1_idx = data->tri_indices[i*3 + 1];
			v2_idx = data->tri_indices[i*3 + 2];
		}

		n0 = VNRM(v0_idx);
		n1 = VNRM(v1_idx);
		n2 = VNRM(v2_idx);

		v0 = VPOS(v0_idx);
		v1 = VPOS(v1_idx);
		v2 = VPOS(v2_idx);

		t0 = VTEX(v0_idx);
		t1 = VTEX(v1_idx);
		t2 = VTEX(v2_idx);

		for(j=0;j<3;j++)
		{
			n = n0;
			if (j == 1)
				n = n1;
			if (j == 2)
				n = n2;
			if (data->tris)
				idx = data->tris[i].idx[j];
			else
				idx = data->tri_indices[i*3 + j];
			tangentBasis( basis, v0, v1, v2, t0, t1, t2, n);
			addVec3(basis[0],tangents[redirects[idx]], tangents[redirects[idx]]);
			addVec3(basis[1],binormals[redirects[idx]], binormals[redirects[idx]]);
		}
	}
	for (i=0;i<data->vert_count;i++)
	{
		if (redirects[i] != i)
		{
			copyVec3(tangents[redirects[i]], tangents[i]);
			copyVec3(binormals[redirects[i]], binormals[i]);
		}
		if (lengthVec3Squared(tangents[i]) < 1.0e-3f)
		{
			if (lengthVec3Squared(binormals[i]) < 1.0e-3f)
			{
				// They're both zero!
				// Use some dummy values, investigate more later?  Don't think this has happened yet...
				setVec3(tangents[i], 1, 0, 0);
				setVec3(binormals[i], 0, 0, 1);
			} else {
				// 0-length tangent, just use cross product of normal and binormal instead
				normalVec3(binormals[i]);
				crossVec3(VNRM(i), binormals[i], tangents[i]);
			}
		} else if (lengthVec3Squared(binormals[i]) < 1.0e-3f) {
			// 0-length binormal, just use cross product of normal and tangent instead
			normalVec3(tangents[i]);
			crossVec3(tangents[i], VNRM(i), binormals[i]);
		}
		if (normalVec3(tangents[i]) < 1.0e-3f)
			++foundBadBasis;
		if (normalVec3(binormals[i]) < 1.0e-3f)
			++foundBadBasis;
		//		if (re-orthoganalize)
		//			orthogonalizeNv(tangents[i], binormals[i], VNRM(i));

		copyVec3(tangents[i],VTANGENT(i));

		if (flip_binormal)
			scaleVec3(binormals[i],-1,VBINORMAL(i));
		else
			copyVec3(binormals[i],VBINORMAL(i));
	}

	ScratchFree(redirects);
	ScratchFree(binormals);
	ScratchFree(tangents);

	PERFINFO_AUTO_STOP();

	return foundBadBasis;
}

//////////////////////////////////////////////////////////////////////////

F32 gmeshReducePrecalced_dbg(GMesh *dst, const GMesh *src, const GMeshReductions *reductions, float target_error, ReductionMethod method MEM_DBG_PARMS)
{
	int i, j, r, tri_count, target_tris;
	float last_error = -1;
	GMesh tempmesh={0};

	int *remaps_ptr = reductions->remaps;
	int *remap_tris_ptr = reductions->remap_tris;

	int *changes_ptr = reductions->changes;
	Vec3 *pos_ptr = reductions->positions, *norm_ptr = reductions->normals;
	Vec2 *tex_ptr = reductions->tex1s;
	U8 *varcolor_ptr = reductions->varcolors;

	assertmsg(method==TRICOUNT_RMETHOD || method==ERROR_RMETHOD || method==COLLAPSE_COUNT_RMETHOD, "gmeshReducePrecalced: Unknown triangle decimation method!");
	assert(dst);

	if (method!=COLLAPSE_COUNT_RMETHOD && target_error > 1)
		target_error = 1;
	if (target_error < 0)
		target_error = 0;

	ZeroStruct(dst);
	gmeshCopy_dbg(&tempmesh, src, 0 MEM_DBG_PARMS_CALL);

	tri_count = tempmesh.tri_count;
	target_tris = (int) (tempmesh.tri_count * (1.f - target_error));

	for (r = 0; r < reductions->num_reductions; r++)
	{
		if (method == TRICOUNT_RMETHOD && tri_count <= target_tris)
			break;
		if (method == ERROR_RMETHOD && reductions->error_values[r] > (target_error+0.0001f))
			break;
		if (method == COLLAPSE_COUNT_RMETHOD && r >= (int)target_error)
			break;

		// triangle remaps
		for (i = 0; i < reductions->remaps_counts[r]; i++)
		{
			int old_idx = *(remaps_ptr++);
			int new_idx = *(remaps_ptr++);
			int num_remap_tris = *(remaps_ptr++);
			for (j = 0; j < num_remap_tris; j++)
				gmeshRemapTriVertex(&tempmesh, *(remap_tris_ptr++), old_idx, new_idx);
		}

		// vertex changes
		for (i = 0; i < reductions->changes_counts[r]; i++)
		{
			int vert_idx = *(changes_ptr++);

			if (pos_ptr && (tempmesh.usagebits & USE_POSITIONS))
				copyVec3(*pos_ptr, tempmesh.positions[vert_idx]);
			if (tex_ptr && (tempmesh.usagebits & USE_TEX1S))
				copyVec2(*tex_ptr, tempmesh.tex1s[vert_idx]);
			if (norm_ptr && (tempmesh.usagebits & USE_NORMALS))
				copyVec3(*norm_ptr, tempmesh.normals[vert_idx]);
			if (varcolor_ptr && (tempmesh.usagebits & USE_VARCOLORS))
				CopyStructs(&tempmesh.varcolors[vert_idx*tempmesh.varcolor_size], varcolor_ptr, tempmesh.varcolor_size);

			if (pos_ptr)
				pos_ptr++;
			if (tex_ptr)
				tex_ptr++;
			if (norm_ptr)
				norm_ptr++;
			if (varcolor_ptr)
				varcolor_ptr += tempmesh.varcolor_size;
		}

		last_error = reductions->error_values[r];
		tri_count = reductions->num_tris_left[r];
	}

	//////////////////////////////////////////////////////////////////////////
	// remove degenerate triangles
	polyGridFree(&tempmesh.grid);
	gmeshMarkDegenerateTris(&tempmesh);

	//////////////////////////////////////////////////////////////////////////
	// merge into destination shape
	gmeshMerge_dbg(dst, &tempmesh, 1, 0, false, false MEM_DBG_PARMS_CALL);
	gmeshFreeData(&tempmesh);

	return last_error;
}

void freeGMeshReductions(GMeshReductions *gmr)
{
	if (!gmr)
		return;

	SAFE_FREE(gmr->num_tris_left);
	SAFE_FREE(gmr->error_values);
	SAFE_FREE(gmr->remaps_counts);
	SAFE_FREE(gmr->changes_counts);
	SAFE_FREE(gmr->remaps);
	SAFE_FREE(gmr->remap_tris);
	SAFE_FREE(gmr->changes);
	SAFE_FREE(gmr->positions);
	SAFE_FREE(gmr->tex1s);
	SAFE_FREE(gmr->normals);
	SAFE_FREE(gmr->varcolors);
	SAFE_FREE(gmr);
}

U32 gmeshCRC(const GMesh *mesh)
{
#define UPDATE(data) cryptAdler32Update((U8 *)(&(data)), sizeof(data))
#define UPDATEVERTS(data) if (data) cryptAdler32Update((U8 *)(data), mesh->vert_count * sizeof(*(data)))
	cryptAdler32Init();
	UPDATE(mesh->usagebits);
	UPDATE(mesh->tri_count);
	UPDATE(mesh->vert_count);
	UPDATEVERTS(mesh->positions);
	UPDATEVERTS(mesh->positions2);
	UPDATEVERTS(mesh->normals);
	UPDATEVERTS(mesh->normals2);
	UPDATEVERTS(mesh->binormals);
	UPDATEVERTS(mesh->tangents);
	UPDATEVERTS(mesh->tex1s);
	UPDATEVERTS(mesh->tex2s);
	UPDATEVERTS(mesh->boneweights);
	UPDATEVERTS(mesh->bonemats);
	UPDATEVERTS(mesh->colors);
	cryptAdler32Update((U8 *)mesh->tris, mesh->tri_count * sizeof(*mesh->tris));
	return cryptAdler32Final();
}

bool gmeshCompare(const GMesh *mesh1, const GMesh *mesh2)
{
	int i;

	if (mesh1->usagebits != mesh2->usagebits)
		return false;
	if (mesh1->tri_count != mesh2->tri_count)
		return false;
	if (mesh1->vert_count != mesh2->vert_count)
		return false;

	if (gmeshCRC(mesh1) == gmeshCRC(mesh2))
		return true;

	if (mesh1->positions)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!nearSameVec3Tol(mesh1->positions[i], mesh2->positions[i], VECTOL))
				return false;
		}
	}

	if (mesh1->positions2)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!nearSameVec3Tol(mesh1->positions2[i], mesh2->positions2[i], VECTOL))
				return false;
		}
	}

	if (mesh1->normals)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!nearSameVec3Tol(mesh1->normals[i], mesh2->normals[i], VECTOL))
				return false;
		}
	}

	if (mesh1->normals2)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!nearSameVec3Tol(mesh1->normals2[i], mesh2->normals2[i], VECTOL))
				return false;
		}
	}

	if (mesh1->binormals)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!nearSameVec3Tol(mesh1->binormals[i], mesh2->binormals[i], VECTOL))
				return false;
		}
	}

	if (mesh1->tangents)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!nearSameVec3Tol(mesh1->tangents[i], mesh2->tangents[i], VECTOL))
				return false;
		}
	}

	if (mesh1->tex1s)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!nearSameVec2(mesh1->tex1s[i], mesh2->tex1s[i]))
				return false;
		}
	}

	if (mesh1->tex2s)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!nearSameVec2(mesh1->tex2s[i], mesh2->tex2s[i]))
				return false;
		}
	}

	if (mesh1->boneweights)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!nearSameVec4(mesh1->boneweights[i], mesh2->boneweights[i]))
				return false;
		}
	}

	if (mesh1->bonemats)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (!sameVec4(mesh1->bonemats[i], mesh2->bonemats[i]))
				return false;
		}
	}

	if (mesh1->colors)
	{
		for (i = 0; i < mesh1->vert_count; ++i)
		{
			if (mesh1->colors[i].integer_for_equality_only != mesh2->colors[i].integer_for_equality_only)
				return false;
		}
	}

	for (i = 0; i < mesh1->tri_count; ++i)
	{
		if (!sameVec3(mesh1->tris[i].idx, mesh2->tris[i].idx))
			return false;
		if (mesh1->tris[i].tex_id != mesh2->tris[i].tex_id)
			return false;
	}

	return true;
}

void gmeshInvertTriangles(GMesh *mesh)
{
	int i;
	for (i = 0; i < mesh->tri_count; ++i)
	{
		GTriIdx *tri = &mesh->tris[i];
		SWAP32(tri->idx[0], tri->idx[2]);
	}
}

static int gmeshTriangleSplit(GMesh *mesh, const Vec3 p0, const Vec3 p1, const Vec3 p2, int *triangle_split_count)
{
	Vec3 tri_min, tri_max;
	int i, *tri_idxs, new_tris = 0;
	Vec4 isect_plane;

	vec3MinMax(p0, p1, tri_min, tri_max);
	vec3RunningMinMax(p2, tri_min, tri_max);
	makePlane(p0, p1, p2, isect_plane);
	
	tri_idxs = polyGridFindTris(&mesh->grid, tri_min, tri_max);
	for (i = 0; i < eaiSize(&tri_idxs); ++i)
	{
		GTriIdx *tri = &mesh->tris[tri_idxs[i]];
		Vec3 isect0, isect1;
		int ret, coplanar;

		ret = tri_tri_intersect_with_isectline(p0, p1, p2, 
			mesh->positions[tri->idx[0]],
			mesh->positions[tri->idx[1]],
			mesh->positions[tri->idx[2]],
			&coplanar, isect0, isect1);

		if (ret && !coplanar)
		{
			int cur_tris = mesh->tri_count;
			int tri_count = gmeshSplitTri(mesh, isect_plane, tri_idxs[i]);
			// Include any new triangles in the clipping process,
			// as edge splits may make new fragments, which of course
			// are not listed in the the poly grid search results.
			for (; cur_tris < mesh->tri_count; ++cur_tris)
				eaiPush(&tri_idxs, cur_tris);

			new_tris += tri_count;

			if (triangle_split_count)
			{
				*triangle_split_count += tri_count;
				if (maxTriangleSplits >= 0 && *triangle_split_count >= maxTriangleSplits)
				{
					printf("");
					break;
				}
			}
		}
	}

	eaiDestroy(&tri_idxs);

	return new_tris;
}

bool gmeshRaycast(GMesh *mesh, const Vec3 ray_start, const Vec3 ray_vec, F32 *collision_time, Vec3 collision_tri_normal)
{
	if (mesh->positions && !mesh->grid.cell)
		gridPolys(&mesh->grid, mesh MEM_DBG_STRUCT_PARMS_CALL(mesh));
	return polyGridIntersectRay(&mesh->grid, mesh, ray_start, ray_vec, collision_time, collision_tri_normal);
}

static int gmeshClassifyTri(GMesh *mesh, const Vec3 p0, const Vec3 p1, const Vec3 p2)
{
	static Vec3 weights[] = {	{1/3.f, 1/3.f, 1/3.f},
								{0.5f, 0.25f, 0.25f},
								{0.25f, 0.5f, 0.25f},
								{0.25f, 0.25f, 0.5f} };
	Vec3 collision_tri_normal;
	F32 collision_time;
	int classification = 1; // outside
	bool need_raycast = true;
	U32 seed = 482502938;
	Vec3 ray_start, ray_vector;
	int w = 0;

	makePlaneNormal(p0, p1, p2, ray_vector);

	while (need_raycast)
	{
		need_raycast = false;

		scaleVec3(p0, weights[w][0], ray_start);
		scaleAddVec3(p1, weights[w][1], ray_start, ray_start);
		scaleAddVec3(p2, weights[w][2], ray_start, ray_start);

		if (gmeshRaycast(mesh, ray_start, ray_vector, &collision_time, collision_tri_normal))
		{
			if (collision_time==0)
			{
				classification = 0; // coplanar triangles
			}
			else
			{
				F32 d = dotVec3(ray_vector, collision_tri_normal);
				if (d > EPSILON)
				{
					// hit triangle faces away, so src triangle is inside
					classification = -1;
				}
				else if (d < -EPSILON)
				{
					// hit triangle faces towards, so src triangle is outside
					classification = 1;
				}
				else
				{
					// perturb ray and redo raycast
					Vec3 perturb;
					randomSphereShellSeeded(&seed, RandType_LCG, 0.025f, perturb);
					addVec3(perturb, ray_vector, ray_vector);
					normalVec3(ray_vector);
					need_raycast = true;
				}
			}
		}
		else if (w < ARRAY_SIZE(weights) - 1)
		{
			++w;
			need_raycast = true;
		}
	}

	return classification;
}

// inside should be -1 if you want to remove tris within the container, 1 if you want to remove tris outside the container
static void gmeshRemoveContainedTris(GMesh *mesh, GMesh *container, int inside, bool remove_shared)
{
	int i, classification_start = !remove_shared;

	if (!container->grid.cell)
		gridPolys(&container->grid, container MEM_DBG_STRUCT_PARMS_CALL(mesh));

	for (i = mesh->tri_count - 1; i >= 0; --i)
	{
		GTriIdx *tri = &mesh->tris[i];
		int classification;
		classification = gmeshClassifyTri(container, mesh->positions[tri->idx[0]], mesh->positions[tri->idx[1]], mesh->positions[tri->idx[2]]);
		if (classification * inside >= classification_start)
			gmeshRemoveTri(mesh, i);
	}

	polyGridFree(&mesh->grid);
	gridPolys(&mesh->grid, mesh MEM_DBG_STRUCT_PARMS_CALL(mesh));
}

void gmeshBoolean_dbg(GMesh *dst, GMesh *src_a, GMesh *src_b, GMeshBooleanOp boolean_op, bool do_op_inverted MEM_DBG_PARMS)
{
	GMesh out = {0};
	GMesh temp = {0};
	int i, triangle_split_count = 0;

	// invert triangles if needed
	if (do_op_inverted)
	{
		gmeshInvertTriangles(src_a);
		gmeshInvertTriangles(src_b);
	}

	// split copy of a with b's triangles
	gmeshCopy_dbg(&out, src_a, true MEM_DBG_PARMS_CALL);
	for (i = 0; i < src_b->tri_count; ++i)
	{
		GTriIdx *src_tri = &src_b->tris[i];

		if (!maxTriangleSplits)
			break;

		if (gmeshTriangleSplit(&out, 
				src_b->positions[src_tri->idx[0]],
				src_b->positions[src_tri->idx[1]],
				src_b->positions[src_tri->idx[2]],
				&triangle_split_count))
		{
			polyGridFree(&out.grid);
			gridPolys(&out.grid, &out MEM_DBG_STRUCT_PARMS_CALL(&out));

			if (maxTriangleSplits >= 0 && triangle_split_count >= maxTriangleSplits)
			{
				printf("");
				break;
			}
		}
	}

	if (boolean_op != BOOLEAN_SUBTRACT_NOFILL && boolean_op != BOOLEAN_SPLIT)
	{
		// split copy of b with a's triangles
		gmeshCopy_dbg(&temp, src_b, true MEM_DBG_PARMS_CALL);
		for (i = 0; i < src_a->tri_count; ++i)
		{
			GTriIdx *src_tri = &src_a->tris[i];
			if (gmeshTriangleSplit(&temp, 
					src_a->positions[src_tri->idx[0]],
					src_a->positions[src_tri->idx[1]],
					src_a->positions[src_tri->idx[2]],
					NULL))
			{
				polyGridFree(&temp.grid);
				gridPolys(&temp.grid, &temp MEM_DBG_STRUCT_PARMS_CALL(&temp));
			}
		}
	}

	// do boolean operation: remove appropriate triangles from the split mesh copies
	switch (boolean_op)
	{
		xcase BOOLEAN_UNION:
		{
			// remove polygons from a's copy that are within b (remove shared)
			gmeshRemoveContainedTris(&out, src_b, -1, true);
			
			// remove polygons from b's copy that are within a (keep shared)
			gmeshRemoveContainedTris(&temp, src_a, -1, false);
		}

		xcase BOOLEAN_INTERSECT:
		{
			// remove polygons from a's copy that are outside b (remove shared)
			gmeshRemoveContainedTris(&out, src_b, 1, true);
		
			// remove polygons from b's copy that are outside a (keep shared)
			gmeshRemoveContainedTris(&temp, src_a, 1, false);
		}

		xcase BOOLEAN_SUBTRACT:
		case BOOLEAN_SUBTRACT_NOFILL:
		{
			// remove polygons from a's copy that are within b (remove shared)
			gmeshRemoveContainedTris(&out, src_b, -1, true);
		
			if (boolean_op == BOOLEAN_SUBTRACT)
			{
				// remove polygons from b's copy that are outside a (remove shared)
				gmeshRemoveContainedTris(&temp, src_a, 1, true);

				// invert b's copy
				gmeshInvertTriangles(&temp);
			}
		}
	}

	if (boolean_op != BOOLEAN_SUBTRACT_NOFILL && boolean_op != BOOLEAN_SPLIT)
	{
		// merge the two meshes together
		gmeshMerge_dbg(&out, &temp, true, true, false, false MEM_DBG_PARMS_CALL);
	}

	// re-invert triangles if needed
	if (do_op_inverted)
	{
		gmeshInvertTriangles(src_a);
		gmeshInvertTriangles(src_b);
		gmeshInvertTriangles(&out);
	}

	// merge data into dst mesh, pooling verts and tris
	gmeshFreeData(dst);
	gmeshMerge_dbg(dst, &out, true, true, false, false MEM_DBG_PARMS_CALL);
	gmeshFreeData(&out);
	gmeshFreeData(&temp);
}

static int box_verts[] = { 0,0,1, 1,0,1, 0,0,0, 1,0,0, 0,1,1, 1,1,1, 0,1,0, 1,1,0 };
static int box_tris[] = { 0,2,3, 3,1,0, 4,5,7, 7,6,4, 0,1,5, 5,4,0, 1,3,7, 7,5,1, 3,2,6, 6,7,3, 2,0,4, 4,6,2 };
void gmeshFromBoundingBox_dbg(GMesh *mesh, const Vec3 local_min, const Vec3 local_max, const Mat4 world_matrix MEM_DBG_PARMS)
{
	Vec3 bounds_extents[2];
	int i;

	gmeshFreeData(mesh);

	gmeshSetUsageBits_dbg(mesh, USE_POSITIONS MEM_DBG_PARMS_CALL);
	gmeshSetVertCount_dbg(mesh, 8 MEM_DBG_PARMS_CALL);
	gmeshSetTriCount_dbg(mesh, 12 MEM_DBG_PARMS_CALL);
	
	copyVec3(local_min, bounds_extents[0]);
	copyVec3(local_max, bounds_extents[1]);
	for (i = 0; i < 8; ++i)
	{
		Vec3 temp_vec;
		setVec3(temp_vec, 
			bounds_extents[box_verts[i*3+0]][0],
			bounds_extents[box_verts[i*3+1]][1],
			bounds_extents[box_verts[i*3+2]][2]);
		mulVecMat4(temp_vec, world_matrix, mesh->positions[i]);
	}

	for (i = 0; i < 12; ++i)
	{
		GTriIdx *tri = &mesh->tris[i];
		tri->idx[0] = box_tris[i*3+0];
		tri->idx[1] = box_tris[i*3+1];
		tri->idx[2] = box_tris[i*3+2];
	}
}

void gmeshFromBoundingBoxWithAttribs_dbg(GMesh *mesh, const Vec3 local_min, const Vec3 local_max, const Mat4 world_matrix MEM_DBG_PARMS)
{
	Vec3 bounds_extents[2];
	Vec3 normal = { 0 };
	Vec3 position;
	Vec2 tex1;
	int i, j;

	gmeshFreeData(mesh);

	gmeshSetUsageBits_dbg(mesh, USE_POSITIONS | USE_TEX1S | USE_NORMALS MEM_DBG_PARMS_CALL);
	gmeshSetVertCount_dbg(mesh, 24 MEM_DBG_PARMS_CALL);
	gmeshSetVertCount_dbg(mesh, 0 MEM_DBG_PARMS_CALL);
	gmeshEnsureTrisFit_dbg(mesh, 12 MEM_DBG_PARMS_CALL);

	copyVec3(local_min, bounds_extents[0]);
	copyVec3(local_max, bounds_extents[1]);
	for (i = 0; i < 3; ++i)
	{
		for (j = 0; j < 2; ++j)
		{
			int xi, yi;
			int vi[4], xa = (i + 1) % 3, ya = (i + 2) % 3;
			normal[i] = j * 2 - 1;

			for (xi = 0; xi < 2; ++xi)
			{
				for (yi = 0; yi < 2; ++yi)
				{
					position[xa] = (xi ? local_max : local_min)[xa];
					position[ya] = (yi ? local_max : local_min)[ya];
					position[i] = (j ? local_max : local_min)[i];
					tex1[0] = xi;
					tex1[1] = yi;
					vi[xi*2 + yi] = gmeshAddVertSimple(mesh, position, normal, tex1, NULL, NULL, NULL, false, true, true);
				}
			}

			if (j)
			{
				SWAP32(vi[0], vi[1]);
				SWAP32(vi[2], vi[3]);
			}

			gmeshAddTri(mesh, vi[0], vi[1], vi[2], 0, false);
			gmeshAddTri(mesh, vi[1], vi[3], vi[2], 0, false);

			normal[i] = 0.0f;
		}
	}
}

GMeshParsed *gmeshToParsedFormat(const GMesh *mesh)
{
	GMeshParsed *mesh_parsed;
	int i;

	if (!mesh)
		return NULL;

	mesh_parsed = StructAlloc(parse_GMeshParsed);
	
	for (i = 0; i < mesh->vert_count; ++i)
	{
		if (mesh->positions)
			eafPush3(&mesh_parsed->positions, mesh->positions[i]);
		if (mesh->positions2)
			eafPush3(&mesh_parsed->positions2, mesh->positions2[i]);
		if (mesh->normals)
			eafPush3(&mesh_parsed->normals, mesh->normals[i]);
		if (mesh->normals2)
			eafPush3(&mesh_parsed->normals2, mesh->normals2[i]);
		if (mesh->binormals)
			eafPush3(&mesh_parsed->binormals, mesh->binormals[i]);
		if (mesh->tangents)
			eafPush3(&mesh_parsed->tangents, mesh->tangents[i]);
		if (mesh->tex1s)
			eafPush2(&mesh_parsed->tex1s, mesh->tex1s[i]);
		if (mesh->tex2s)
			eafPush2(&mesh_parsed->tex2s, mesh->tex2s[i]);
		if (mesh->boneweights)
			eafPush4(&mesh_parsed->boneweights, mesh->boneweights[i]);
		if (mesh->bonemats)
		{
			eaiPush(&mesh_parsed->bonemats, mesh->bonemats[i][0]);
			eaiPush(&mesh_parsed->bonemats, mesh->bonemats[i][1]);
			eaiPush(&mesh_parsed->bonemats, mesh->bonemats[i][2]);
			eaiPush(&mesh_parsed->bonemats, mesh->bonemats[i][3]);
		}
		if (mesh->colors)
		{
			eaiPush(&mesh_parsed->colors, mesh->colors[i].r);
			eaiPush(&mesh_parsed->colors, mesh->colors[i].g);
			eaiPush(&mesh_parsed->colors, mesh->colors[i].b);
			eaiPush(&mesh_parsed->colors, mesh->colors[i].a);
		}
	}

	for (i = 0; i < mesh->tri_count; ++i)
	{
		eaiPush(&mesh_parsed->tris, mesh->tris[i].idx[0]);
		eaiPush(&mesh_parsed->tris, mesh->tris[i].idx[1]);
		eaiPush(&mesh_parsed->tris, mesh->tris[i].idx[2]);
		eaiPush(&mesh_parsed->tris, mesh->tris[i].tex_id);
	}

	return mesh_parsed;
}

int gmeshParsedGetSize(const GMeshParsed *mesh_parsed)
{
	int size = sizeof(GMeshParsed);

	if (!mesh_parsed)
		return 0;

	size += eafSize(&mesh_parsed->positions) * sizeof(F32);
	size += eafSize(&mesh_parsed->positions2) * sizeof(F32);
	size += eafSize(&mesh_parsed->normals) * sizeof(F32);
	size += eafSize(&mesh_parsed->normals2) * sizeof(F32);
	size += eafSize(&mesh_parsed->binormals) * sizeof(F32);
	size += eafSize(&mesh_parsed->tangents) * sizeof(F32);
	size += eafSize(&mesh_parsed->tex1s) * sizeof(F32);
	size += eafSize(&mesh_parsed->tex2s) * sizeof(F32);
	size += eafSize(&mesh_parsed->boneweights) * sizeof(F32);
	size += eaiSize(&mesh_parsed->bonemats) * sizeof(int);
	size += eaiSize(&mesh_parsed->colors) * sizeof(int);
	size += eaiSize(&mesh_parsed->tris) * sizeof(int);

	return size;
}

GMesh *gmeshFromParsedFormat_dbg(const GMeshParsed *mesh_parsed MEM_DBG_PARMS)
{
	int i, usagebits = 0, vert_count = 0, test_vert_count = 0;
	GMesh *mesh;

	if (!mesh_parsed)
		return NULL;

	mesh = scalloc(1, sizeof(GMesh));

	if (!mesh->caller_fname)
	{
		MEM_DBG_STRUCT_PARMS_INIT(mesh);
	}

	if (mesh_parsed->positions)
	{
		test_vert_count = eafSize(&mesh_parsed->positions)/3;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_POSITIONS;
		}
	}
	if (mesh_parsed->positions2)
	{
		test_vert_count = eafSize(&mesh_parsed->positions2)/3;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_POSITIONS2;
		}
	}
	if (mesh_parsed->normals)
	{
		test_vert_count = eafSize(&mesh_parsed->normals)/3;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_NORMALS;
		}
	}
	if (mesh_parsed->normals2)
	{
		test_vert_count = eafSize(&mesh_parsed->normals2)/3;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_NORMALS2;
		}
	}
	if (mesh_parsed->binormals)
	{
		test_vert_count = eafSize(&mesh_parsed->binormals)/3;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_BINORMALS;
		}
	}
	if (mesh_parsed->tangents)
	{
		test_vert_count = eafSize(&mesh_parsed->tangents)/3;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_TANGENTS;
		}
	}
	if (mesh_parsed->tex1s)
	{
		test_vert_count = eafSize(&mesh_parsed->tex1s)/2;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_TEX1S;
		}
	}
	if (mesh_parsed->tex2s)
	{
		test_vert_count = eafSize(&mesh_parsed->tex2s)/2;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_TEX2S;
		}
	}
	if (mesh_parsed->boneweights)
	{
		test_vert_count = eafSize(&mesh_parsed->boneweights)/4;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_BONEWEIGHTS;
		}
	}
	if (mesh_parsed->colors)
	{
		test_vert_count = eaiSize(&mesh_parsed->colors)/4;
		assert(!vert_count || !test_vert_count || vert_count == test_vert_count);
		if (test_vert_count)
		{
			vert_count = test_vert_count;
			usagebits |= USE_COLORS;
		}
	}
	// DJR disabling since we are making some occlusion meshes with zero verts, and while they are not
	// technically broken, they make this assert fire. This may be too general of a place for this
	// assert.
	//assert(vert_count);
	gmeshSetVertCount_dbg(mesh, vert_count MEM_DBG_PARMS_CALL);

	gmeshSetTriCount_dbg(mesh, eaiSize(&mesh_parsed->tris)/4 MEM_DBG_PARMS_CALL);
	gmeshSetUsageBits_dbg(mesh, usagebits MEM_DBG_PARMS_CALL);

	for (i = 0; i < mesh->vert_count; ++i)
	{
		if (mesh_parsed->positions)
			copyVec3(&mesh_parsed->positions[i*3], mesh->positions[i]);
		if (mesh_parsed->positions2)
			copyVec3(&mesh_parsed->positions2[i*3], mesh->positions2[i]);
		if (mesh_parsed->normals)
			copyVec3(&mesh_parsed->normals[i*3], mesh->normals[i]);
		if (mesh_parsed->normals2)
			copyVec3(&mesh_parsed->normals2[i*3], mesh->normals2[i]);
		if (mesh_parsed->binormals)
			copyVec3(&mesh_parsed->binormals[i*3], mesh->binormals[i]);
		if (mesh_parsed->tangents)
			copyVec3(&mesh_parsed->tangents[i*3], mesh->tangents[i]);
		if (mesh_parsed->tex1s)
			copyVec2(&mesh_parsed->tex1s[i*2], mesh->tex1s[i]);
		if (mesh_parsed->tex2s)
			copyVec2(&mesh_parsed->tex2s[i*2], mesh->tex2s[i]);
		if (mesh_parsed->boneweights && eafSize(&mesh_parsed->boneweights))
			copyVec4(&mesh_parsed->boneweights[i*4], mesh->boneweights[i]);
		if (mesh_parsed->bonemats && eafSize(&mesh_parsed->bonemats))
			copyVec4(&mesh_parsed->bonemats[i*4], mesh->bonemats[i]);
		if (mesh_parsed->colors && eaiSize(&mesh_parsed->colors))
			mesh->colors[i] = CreateColor(mesh_parsed->colors[i*4], mesh_parsed->colors[i*4+1], mesh_parsed->colors[i*4+2], mesh_parsed->colors[i*4+3]);
	}

	assert(mesh_parsed->tris || !mesh->tri_count);
	for (i = 0; i < mesh->tri_count; ++i)
	{
		mesh->tris[i].idx[0] = mesh_parsed->tris[i*4+0];
		mesh->tris[i].idx[1] = mesh_parsed->tris[i*4+1];
		mesh->tris[i].idx[2] = mesh_parsed->tris[i*4+2];
		mesh->tris[i].tex_id = mesh_parsed->tris[i*4+3];
	}

	return mesh;
}

U32 gmeshGetBinSize(const GMesh *mesh)
{
	U32 total_size = sizeof(U32); // holds byte count

	if (!mesh)
		return total_size;

	total_size += sizeof(U32)  // holds usage bits
				+ sizeof(U32)  // holds vert count
				+ sizeof(U32)  // holds tri count
				+ sizeof(U32); // holds varcolor size

	total_size += mesh->tri_count * sizeof(GTriIdx); // tris

	if (mesh->positions)
		total_size += mesh->vert_count * sizeof(Vec3);
	if (mesh->positions2)
		total_size += mesh->vert_count * sizeof(Vec3);
	if (mesh->normals)
		total_size += mesh->vert_count * sizeof(Vec3);
	if (mesh->normals2)
		total_size += mesh->vert_count * sizeof(Vec3);
	if (mesh->binormals)
		total_size += mesh->vert_count * sizeof(Vec3);
	if (mesh->tangents)
		total_size += mesh->vert_count * sizeof(Vec3);
	if (mesh->tex1s)
		total_size += mesh->vert_count * sizeof(Vec2);
	if (mesh->tex2s)
		total_size += mesh->vert_count * sizeof(Vec2);
	if (mesh->boneweights)
		total_size += mesh->vert_count * sizeof(Vec4);
	if (mesh->bonemats)
		total_size += mesh->vert_count * 4 * sizeof(U16);
	if (mesh->colors)
		total_size += mesh->vert_count * 4 * sizeof(U8);
	if (mesh->varcolors)
		total_size += mesh->vert_count * mesh->varcolor_size * sizeof(U8);

	return total_size;
}

void gmeshWriteBinData(const GMesh *mesh, SimpleBufHandle buf)
{
	U32 total_bytes;
	int i;

	total_bytes = gmeshGetBinSize(mesh);
	SimpleBufWriteU32(total_bytes, buf);

	if (!mesh)
		return;

	SimpleBufWriteU32(mesh->usagebits, buf);
	SimpleBufWriteU32(mesh->vert_count, buf);
	SimpleBufWriteU32(mesh->tri_count, buf);
	SimpleBufWriteU32(mesh->varcolor_size, buf);

	for (i = 0; i < mesh->tri_count; ++i)
	{
		SimpleBufWriteU32(mesh->tris[i].idx[0], buf);
		SimpleBufWriteU32(mesh->tris[i].idx[1], buf);
		SimpleBufWriteU32(mesh->tris[i].idx[2], buf);
		SimpleBufWriteU16(mesh->tris[i].tex_id, buf);
	}

	if (mesh->usagebits & USE_POSITIONS)
		SimpleBufWriteF32Array(&mesh->positions[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_POSITIONS2)
		SimpleBufWriteF32Array(&mesh->positions2[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_NORMALS)
		SimpleBufWriteF32Array(&mesh->normals[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_NORMALS2)
		SimpleBufWriteF32Array(&mesh->normals2[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_BINORMALS)
		SimpleBufWriteF32Array(&mesh->binormals[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_TANGENTS)
		SimpleBufWriteF32Array(&mesh->tangents[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_TEX1S)
		SimpleBufWriteF32Array(&mesh->tex1s[0][0], mesh->vert_count*2, buf);

	if (mesh->usagebits & USE_TEX2S)
		SimpleBufWriteF32Array(&mesh->tex2s[0][0], mesh->vert_count*2, buf);

	if (mesh->usagebits & USE_BONEWEIGHTS)
	{
		SimpleBufWriteF32Array(&mesh->boneweights[0][0], mesh->vert_count*4, buf);
		SimpleBufWriteU16Array(&mesh->bonemats[0][0], mesh->vert_count*4, buf);
	}

	if (mesh->usagebits & USE_COLORS)
		SimpleBufWrite(&mesh->colors[0], mesh->vert_count*4*sizeof(U8), buf);

	if (mesh->usagebits & USE_VARCOLORS)
		SimpleBufWrite(mesh->varcolors, mesh->vert_count*mesh->varcolor_size*sizeof(U8), buf);

	if (mesh->usagebits & USE_OPTIONAL_PARSE_STRUCT) {
		ParseTable *pTable;
		char *pTableText = NULL;

		estrStackCreate(&pTableText);
		SimpleBufWriteString(mesh->parse_table,buf);
		pTable = ParserGetTableFromStructName(mesh->parse_table);
		ParserWriteText(&pTableText,pTable,mesh->parse_struct_data,0,0,0);
		SimpleBufWriteString(pTableText,buf);
		estrDestroy(&pTableText);
	}
}

GMesh *gmeshFromBinData_dbg(SimpleBufHandle buf MEM_DBG_PARMS)
{
	U32 total_bytes, vert_count, tri_count, varcolor_size;
	int i, usagebits;
	GMesh *mesh;

	SimpleBufReadU32(&total_bytes, buf);

	if (total_bytes <= 4)
		return NULL;

	mesh = scalloc(1, sizeof(GMesh));

	if (!mesh->caller_fname)
	{
		MEM_DBG_STRUCT_PARMS_INIT(mesh);
	}

	SimpleBufReadU32(&usagebits, buf);
	SimpleBufReadU32(&vert_count, buf);
	SimpleBufReadU32(&tri_count, buf);
	SimpleBufReadU32(&varcolor_size, buf);

	mesh->vert_count = mesh->vert_max = vert_count;
	gmeshEnsureTrisFit_dbg(mesh, tri_count MEM_DBG_PARMS_CALL);
	gmeshSetUsageBits_dbg(mesh, usagebits MEM_DBG_PARMS_CALL);
	mesh->tri_count = tri_count;
	gmeshSetVarColorSize_dbg(mesh, varcolor_size MEM_DBG_PARMS_CALL);

	for (i = 0; i < mesh->tri_count; ++i)
	{
		SimpleBufReadU32(&mesh->tris[i].idx[0], buf);
		SimpleBufReadU32(&mesh->tris[i].idx[1], buf);
		SimpleBufReadU32(&mesh->tris[i].idx[2], buf);
		SimpleBufReadU16(&mesh->tris[i].tex_id, buf);
	}

	if (mesh->usagebits & USE_POSITIONS)
		SimpleBufReadF32Array(&mesh->positions[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_POSITIONS2)
		SimpleBufReadF32Array(&mesh->positions2[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_NORMALS)
		SimpleBufReadF32Array(&mesh->normals[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_NORMALS2)
		SimpleBufReadF32Array(&mesh->normals2[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_BINORMALS)
		SimpleBufReadF32Array(&mesh->binormals[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_TANGENTS)
		SimpleBufReadF32Array(&mesh->tangents[0][0], mesh->vert_count*3, buf);

	if (mesh->usagebits & USE_TEX1S)
		SimpleBufReadF32Array(&mesh->tex1s[0][0], mesh->vert_count*2, buf);

	if (mesh->usagebits & USE_TEX2S)
		SimpleBufReadF32Array(&mesh->tex2s[0][0], mesh->vert_count*2, buf);

	if (mesh->usagebits & USE_BONEWEIGHTS)
	{
		SimpleBufReadF32Array(&mesh->boneweights[0][0], mesh->vert_count*4, buf);
		SimpleBufReadU16Array(&mesh->bonemats[0][0], mesh->vert_count*4, buf);
	}

	if (mesh->usagebits & USE_COLORS)
		SimpleBufRead(&mesh->colors[0], mesh->vert_count*4*sizeof(U8), buf);

	if (mesh->usagebits & USE_VARCOLORS)
		SimpleBufRead(mesh->varcolors, mesh->vert_count*mesh->varcolor_size*sizeof(U8), buf);

	if (mesh->usagebits & USE_OPTIONAL_PARSE_STRUCT) {
		ParseTable *pTable;
		char *parse_table_name = NULL;
		char *parseTableString = NULL;

		SimpleBufReadString(&parse_table_name,buf);
		mesh->parse_table = allocAddString(parse_table_name);
		pTable = ParserGetTableFromStructName(mesh->parse_table);
		SimpleBufReadString(&parseTableString,buf);
		mesh->parse_struct_data = StructCreateVoid(pTable);
		ParserReadText(parseTableString,pTable,mesh->parse_struct_data, PARSER_NO_RELOAD | PARSER_CLIENTSIDE);
	}

	return mesh;
}

F32 gmeshUvDensity(const GMesh *mesh)
{
	const Vec3 *verts = mesh->positions;
	const Vec2 *texcoords = mesh->tex1s;
	const GTriIdx *tris = mesh->tris;
	F32 density =  FLT_MAX;
	int i;

	if (!mesh->positions || !mesh->tex1s || !mesh->tri_count) {
		return GMESH_LOG_MIN_UV_DENSITY;
	}

	for (i=0; i<mesh->tri_count; i++)
	{
		F32 tria = triArea3Squared(verts[tris[i].idx[0]], verts[tris[i].idx[1]], verts[tris[i].idx[2]]);
		F32 uva = triArea2(texcoords[tris[i].idx[0]], texcoords[tris[i].idx[1]], texcoords[tris[i].idx[2]]);

		if (tria > 0.000001f && uva > 0.000001f) {
			F32 tri_density;

			F32 duv0 = distance2Squared(texcoords[tris[i].idx[0]], texcoords[tris[i].idx[1]]);
			F32 duv1 = distance2Squared(texcoords[tris[i].idx[1]], texcoords[tris[i].idx[2]]);
			F32 duv2 = distance2Squared(texcoords[tris[i].idx[2]], texcoords[tris[i].idx[0]]);

			duv0 /= distance3Squared(verts[tris[i].idx[0]], verts[tris[i].idx[1]]);
			duv1 /= distance3Squared(verts[tris[i].idx[1]], verts[tris[i].idx[2]]);
			duv2 /= distance3Squared(verts[tris[i].idx[2]], verts[tris[i].idx[0]]);

			tri_density = MAX(MAX(duv0, duv1), duv2);
			MIN1F(density, tri_density);
		}
	}

	if (density == FLT_MAX || density < GMESH_MIN_UV_DENSITY) {
		density = GMESH_LOG_MIN_UV_DENSITY;
	} else {
		density = 0.5f * log2f(density);
	}

	return density;
}



#include "AutoGen/GenericMesh_h_ast.c"

