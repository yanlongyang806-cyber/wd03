#include "GenericMeshPrivate.h"
#include "wininclude.h"
#include "ScratchStack.h"
#include "textparser.h"
#include "SimplygonInterface.h"
#include "error.h"
#include "logging.h"
#include "StringCache.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););

AUTO_RUN_ANON(memBudgetAddStructMappingIfNotMapped("IQuadric", __FILE__););
AUTO_RUN_ANON(memBudgetAddStructMappingIfNotMapped("IVec", __FILE__););

static int tricut_counter;

//////////////////////////////////////////////////////////////////////////

static int countLiveTris(TriCutType *tc)
{
	int i, count = 0;

	for (i = 0; i < tc->mesh->tri_count; i++)
	{
		if (!tc->triinfos[i].is_dead)
			count++;
	}

	return count;
}

void checkVerts(TriCutType *tc)
{
	int i, j, k, idx;
	for (i = 0; i < eaSize(&tc->svertinfos); i++)
	{
		SVertInfo *svert = tc->svertinfos[i];
		for (j = 0; j < eaiSize(&svert->faces); j++)
		{
			TriInfo *tri = &tc->triinfos[svert->faces[j]];
			for (k = 0; k < 3; k++)
			{
				if (tri->is_dead)
				{
					assert(!tri->sedges[k]);
				}
				else
				{
					assert(tri->sedges[k]);
					idx = eaiFind(&tri->sedges[k]->faces, svert->faces[j]);
					assert(idx >= 0);
				}
			}
		}
	}
}

void checkEdges(TriCutType *tc)
{
	int i, j, count;
	count = eaSize(&tc->internal_edges);
	for (i = 0; i < count; i++)
	{
		IEdge *e = tc->internal_edges[i];
		int v1 = e->ivert1;
		int v2 = e->ivert2;
		int tex = e->tex_id;

		assert(v1 != v2);
		assert(!tc->ivertinfos[v1].is_dead);
		assert(!tc->ivertinfos[v2].is_dead);

		for (j = i+1; j < count; j++)
		{
			assert(!edgeMatch(tc->internal_edges[j], v1, v2, tex));
		}

		j = eaiFind(&tc->svertinfos[e->sedge->svert1]->iverts, v1);
		assert(j >= 0);

		j = eaiFind(&tc->svertinfos[e->sedge->svert2]->iverts, v2);
		assert(j >= 0);
	}

	for (j = 0; j < BUCKET_COUNT; ++j)
	{
		count = eaSize(&tc->edge_buckets[j].super_edges);
		assert(count == eaiSize(&tc->edge_buckets[j].super_edge_costs));
		for (i = 0; i < count; i++)
		{
			SEdge *se = tc->edge_buckets[j].super_edges[i];
			assert(!tc->svertinfos[se->svert1]->is_dead);
			assert(!tc->svertinfos[se->svert2]->is_dead);
			assert((int)se->array_index == i);
		}
	}
}

void checkTris(TriCutType *tc)
{
	int i;
	for (i = 0; i < tc->mesh->tri_count; i++)
	{
		assert(tc->triinfos[i].is_dead || !isTriDegenerate(tc, &tc->mesh->tris[i]));
	}

	assert(tc->live_tris == countLiveTris(tc));
}

void checkLogMatch(FILE *logfile, const char *fmt, ...)
{
	char logstr[2048];
	int logstrlen;
	char *str = NULL;

	VA_START(args, fmt);
	estrConcatfv(&str, fmt, args);
	VA_END();

	logstr[0] = 0;
	fgets(logstr, ARRAY_SIZE(logstr)-1, logfile);

	logstrlen = (int)strlen(logstr);
	if (logstrlen && logstr[logstrlen-1] == 10)
	{
		ANALYSIS_ASSUME(logstrlen > 0);
		logstr[logstrlen-1] = 0;
	}

	if (strcmp(logstr, str) != 0)
		_DbgBreak();

	estrDestroy(&str);
}

static void subVec3Func(const Vec3 v1, const Vec3 v2, Vec3 r)
{
	subVec3(v1, v2, r);
}

static void subVec4Func(const Vec4 v1, const Vec4 v2, Vec4 r)
{
	subVec4(v1, v2, r);
}

static void mulVecVec3Func(const Vec3 v1, const Vec3 v2, Vec3 r)
{
	mulVecVec3(v1, v2, r);
}

static void mulVecVec4Func(const Vec4 v1, const Vec4 v2, Vec4 r)
{
	mulVecVec4(v1, v2, r);
}

static void initializeIFunctions(TriCutType *tc)
{
#define INITFUNCS1_b(b) tc->ivec_size = sizeof(Vec ## b);			\
						tc->iquadric_size = sizeof(Quadric ## b);	\
						tc->evaluateIQ = (evaluateIQFunc)(evaluateQ ## b);			\
						tc->addIQ = (addIQFunc)(addQ ## b);						\
						tc->optimizeIQ = (optimizeIQFunc)(optimizeQ ## b);			\
						tc->initFromTriIQ = (initFromTriIQFunc)(initFromTriQ ## b);		\
						tc->addToIQ = (addToIQFunc)(addToQ ## b)

#define INITFUNCS2_b(b)	tc->subIVec = (IVecFunc)(subVec ## b);		\
						tc->mulIVecIVec = (IVecFunc)(mulVecVec ## b)

#define INITFUNCS1(b) INITFUNCS1_b(b)
#define INITFUNCS2(b) INITFUNCS2_b(b)
#define INITALL(b) xcase (b): INITFUNCS1_b(b); INITFUNCS2_b(b)

	switch (tc->icount)
	{
		xcase 3:
			INITFUNCS1(3);
			tc->subIVec = (IVecFunc)subVec3Func;
			tc->mulIVecIVec = (IVecFunc)mulVecVec3Func;

		xcase 4:
			INITFUNCS1(4);
			tc->subIVec = (IVecFunc)subVec4Func;
			tc->mulIVecIVec = (IVecFunc)mulVecVec4Func;

		INITALL(5);
		INITALL(6);
		INITALL(7);
		INITALL(8);
		INITALL(9);
		INITALL(10);
		INITALL(11);
		INITALL(12);
		INITALL(13);
		INITALL(14);
		INITALL(15);
		INITALL(16);
		INITALL(17);
		INITALL(18);
		INITALL(19);
		INITALL(20);
		INITALL(21);
		INITALL(22);
		INITALL(23);
		INITALL(24);
		INITALL(25);
		INITALL(26);
		INITALL(27);
		INITALL(28);
		INITALL(29);
		INITALL(30);
		INITALL(31);
		INITALL(32);
		INITALL(33);

		xdefault:
			assert(0);
	}

	tc->MP_NAME(IVec) = createMemoryPoolNamed("IVec" MEM_DBG_PARMS_INIT);
	initMemoryPool(tc->MP_NAME(IVec), tc->ivec_size, 256);
	mpSetMode(tc->MP_NAME(IVec), ZeroMemoryBit);

	tc->MP_NAME(IQuadric) = createMemoryPoolNamed("IQuadric" MEM_DBG_PARMS_INIT);
	initMemoryPool(tc->MP_NAME(IQuadric), tc->iquadric_size, 256);
	mpSetMode(tc->MP_NAME(IQuadric), ZeroMemoryBit);
}

static TriCutType *createTriCut(GMesh *mesh, F32 scale_by_area, bool use_optimal_placement, bool maintain_borders, bool is_terrain, const char *debug_name)
{
	int i, j;
	TriCutType *tc = calloc(1, sizeof(*tc));
	Vec3 bounds_min = {8e16, 8e16, 8e16}, bounds_max = {-8e16, -8e16, -8e16};
	Vec2 tex_bounds_min = {8e16, 8e16}, tex_bounds_max = {-8e16, -8e16};
	char path[MAX_PATH];

	if (ENABLE_LOGGING)
	{
		InterlockedIncrement(&tricut_counter);
		if (debug_name)
			sprintf(path, "%s/reduce_%s.log", fileTempDir(), debug_name);
		else
			sprintf(path, "%s/reducelog_%03d.log", fileTempDir(), tricut_counter);
		tc->logfile = fopen(path, "wt");
	}
	else if (CHECK_LOG)
	{
		InterlockedIncrement(&tricut_counter);
		if (debug_name)
			sprintf(path, "%s/reduce_%s.log", fileTempDir(), debug_name);
		else
			sprintf(path, "%s/reducelog_%03d.log", fileTempDir(), tricut_counter);
		tc->logfile = fopen(path, "rt");
	}

	LOG("createTriCut");

	gmeshSort(mesh);

	if (ENABLE_MESH_LOGGING)
	{
		GMeshParsed *mesh_parsed = gmeshToParsedFormat(mesh);
		if (debug_name)
			sprintf(path, "%s/reduce_pre_%s.msh", fileTempDir(), debug_name);
		else
			sprintf(path, "%s/reduce_pre_d%03d.msh", fileTempDir(), tricut_counter);
		ParserWriteTextFile(path, parse_GMeshParsed, mesh_parsed, 0, 0);
		StructDestroy(parse_GMeshParsed, mesh_parsed);
	}

	for (i = 0; i < mesh->vert_count; ++i)
		vec3RunningMinMax(mesh->positions[i], bounds_min, bounds_max);

	if (mesh->tex1s)
	{
		for (i = 0; i < mesh->vert_count; ++i)
			vec2RunningMinMax(mesh->tex1s[i], tex_bounds_min, tex_bounds_max);
	}
	else
	{
		zeroVec2(tex_bounds_min);
		zeroVec2(tex_bounds_max);
	}

	tc->mesh = mesh;
	tc->live_tris = mesh->tri_count;

	tc->scale_by_area = saturate(scale_by_area);
	tc->use_optimal_placement = use_optimal_placement;
	tc->maintain_borders = maintain_borders;
	tc->is_terrain = is_terrain;
	tc->bucket_cost_divisor = UNCOLLAPSIBLE_COST / (BUCKET_COUNT-2);
	tc->one_over_max_tri_area = 1;

	tc->orig_tri_count = mesh->tri_count;
	tc->triinfos = calloc(sizeof(*tc->triinfos), mesh->tri_count);
	tc->orig_vert_count = mesh->vert_count;
	tc->ivertinfos = calloc(sizeof(*tc->ivertinfos), mesh->vert_count);

	tc->icount = 3;
	if (mesh->usagebits & USE_TEX1S)
		tc->icount += 2;
	if (mesh->usagebits & USE_NORMALS)
		tc->icount += 3;
	if (mesh->usagebits & USE_COLORS)
		tc->icount += 4;
	if (mesh->usagebits & USE_VARCOLORS)
		tc->icount += mesh->varcolor_size;

	initializeIFunctions(tc);

	tc->bounds_min = MP_ALLOC_MEMBER_NOPTRCAST(tc, IVec);
	tc->bounds_max = MP_ALLOC_MEMBER_NOPTRCAST(tc, IVec);
	tc->multiplier = MP_ALLOC_MEMBER_NOPTRCAST(tc, IVec);
	tc->zeroivec = MP_ALLOC_MEMBER_NOPTRCAST(tc, IVec);

	copyVec3(bounds_min, tc->bounds_min);
	copyVec3(bounds_max, tc->bounds_max);
	i = 3;

	if (mesh->usagebits & USE_TEX1S)
	{
		copyVec2(tex_bounds_min, &tc->bounds_min[i]);
		copyVec2(tex_bounds_max, &tc->bounds_max[i]);
		i += 2;
	}

	if (mesh->usagebits & USE_NORMALS)
	{
		setVec3same(&tc->bounds_min[i], 0);
		setVec3same(&tc->bounds_max[i], 1);
		i += 3;
	}

	if (mesh->usagebits & USE_COLORS)
	{
		setVec4same(&tc->bounds_min[i], 0);
		setVec4same(&tc->bounds_max[i], 255.f / 2.f); // make color differences more important
		i += 4;
	}

	if (mesh->usagebits & USE_VARCOLORS)
	{
		for (j = 0; j < mesh->varcolor_size; ++j)
		{
			tc->bounds_min[i+j] = 0;
			tc->bounds_max[i+j] = 255.f / 2.f; // make varcolor differences more important
		}
		i += mesh->varcolor_size;
	}
	
	tc->subIVec(tc->bounds_max, tc->bounds_min, tc->multiplier);
	MAX1(tc->multiplier[0], tc->multiplier[1]);
	MAX1(tc->multiplier[0], tc->multiplier[2]);
	tc->multiplier[1] = tc->multiplier[2] = tc->multiplier[0];
	for (i = 0; i < tc->icount; ++i)
		tc->multiplier[i] = nearSameF32(tc->multiplier[i], 0) ? 1.f : (1.f / tc->multiplier[i]);

	for (i = 0; i < mesh->vert_count; i++)
	{
		tc->ivertinfos[i].global_error = MP_ALLOC_MEMBER(tc, IQuadric);
		tc->ivertinfos[i].local_error = MP_ALLOC_MEMBER(tc, IQuadric);
		tc->ivertinfos[i].attributes = MP_ALLOC_MEMBER_NOPTRCAST(tc, IVec);
		setVertAttributes(tc, i);
	}
	freeVertexHashData(&tc->svert_hash);

	for (i = 0; i < mesh->tri_count; i++)
		calculateTriInfo(tc, i, 1);

	// mark boundary verts
	for (j = 0; j < BUCKET_COUNT; ++j)
	{
		for (i = 0; i < eaSize(&tc->edge_buckets[j].super_edges); i++)
		{
			SEdge *e = tc->edge_buckets[j].super_edges[i];
			if (edgeIsBoundary(tc, e))
				tc->svertinfos[e->svert1]->on_boundary = tc->svertinfos[e->svert2]->on_boundary = 1;
			if (edgeIsCrease(tc, e))
				tc->svertinfos[e->svert1]->on_crease = tc->svertinfos[e->svert2]->on_crease = 1;

			if (eaSize(&e->iedges) > 1)
				tc->svertinfos[e->svert1]->on_texedge = tc->svertinfos[e->svert2]->on_texedge = 1;
			else
				tc->svertinfos[e->svert1]->on_texedge = tc->svertinfos[e->svert2]->on_texedge = 0;
		}
	}

	for (i = 0; i < eaSize(&tc->svertinfos); i++)
	{
		SVertInfo *svert = tc->svertinfos[i];
		svert->on_texedge = svert->on_texedge || eaiSize(&svert->iverts) > 1;
		svert->is_locked = tc->is_terrain && svert->on_boundary;
		if (svert->is_locked)
		{
			Vec2 v[2];
			int idx = 0;
			for (j = 0; idx < 2 && j < eaSize(&svert->sedges); ++j)
			{
				SEdge *sedge = svert->sedges[j];
				if (!(eaSize(&sedge->iedges) == 1 && eaiSize(&sedge->faces) == 1))
					continue;
				if (svert == tc->svertinfos[sedge->svert1])
					setVec2(v[idx], tc->svertinfos[sedge->svert2]->v[0] - svert->v[0], tc->svertinfos[sedge->svert2]->v[2] - svert->v[2]);
				else
					setVec2(v[idx], tc->svertinfos[sedge->svert1]->v[0] - svert->v[0], tc->svertinfos[sedge->svert1]->v[2] - svert->v[2]);
				++idx;
			}
			if (idx == 2)
			{
				normalVec2(v[0]);
				normalVec2(v[1]);
				svert->is_locked = -dotVec2(v[0], v[1]) < 0.9f;
			}
			else
			{
				svert->is_locked = 0;
			}
		}
	}

	computeAllQuadrics(tc);

	if (DEBUG_CHECKS)
	{
		checkVerts(tc);
		checkEdges(tc);
		checkTris(tc);
	}

	return tc;
}

static void freeTriCut(TriCutType *tc)
{
	int i, j;

	for (i = eaSize(&tc->internal_edges)-1; i >= 0; --i)
		freeIEdge(tc, tc->internal_edges[i]);
	eaDestroy(&tc->internal_edges);

	for (j = 0; j < BUCKET_COUNT; ++j)
	{
		for (i = eaSize(&tc->edge_buckets[j].super_edges)-1; i >= 0; --i)
			freeSEdge(tc, tc->edge_buckets[j].super_edges[i]);
		eaDestroy(&tc->edge_buckets[j].super_edges);
		eaiDestroy(&tc->edge_buckets[j].super_edge_costs);
	}

	for (i = eaSize(&tc->svertinfos)-1; i >= 0; --i)
		freeSVert(tc, tc->svertinfos[i]);
	eaDestroy(&tc->svertinfos);

	for (i = 0; i < tc->orig_vert_count; i++)
	{
		eaDestroy(&tc->ivertinfos[i].iedges);
		MP_FREE_MEMBER(tc, IQuadric, tc->ivertinfos[i].global_error);
		MP_FREE_MEMBER(tc, IQuadric, tc->ivertinfos[i].local_error);
		MP_FREE_MEMBER(tc, IVec, tc->ivertinfos[i].attributes);
	}

	MP_FREE_MEMBER(tc, IVec, tc->zeroivec);
	MP_FREE_MEMBER(tc, IVec, tc->multiplier);
	MP_FREE_MEMBER(tc, IVec, tc->bounds_max);
	MP_FREE_MEMBER(tc, IVec, tc->bounds_min);

	SAFE_FREE(tc->triinfos);
	SAFE_FREE(tc->ivertinfos);

	MP_DESTROY_MEMBER(tc, IEdge);
	MP_DESTROY_MEMBER(tc, SEdge);
	MP_DESTROY_MEMBER(tc, SVertInfo);
	MP_DESTROY_MEMBER(tc, ReduceInstruction);

	MP_DESTROY_MEMBER(tc, IVec);
	MP_DESTROY_MEMBER(tc, IQuadric);

	eaiDestroy(&tc->faces_array1);
	eaiDestroy(&tc->faces_array2);
	eaDestroy(&tc->changed_edges);
	eaiDestroy(&tc->changed_faces);
	eaiDestroy(&tc->changed_iverts);
	eaiDestroy(&tc->dead_idxs);
	eaiDestroy(&tc->new_idxs);

	LOG("freeTriCut");

	if (tc->logfile)
		fclose(tc->logfile);

	freeVertexHashData(&tc->svert_hash);

	free(tc);
}

static F32 cost_lookup[11] = 
{
	0.f,		// 0.0
	0.01f,		// 0.1
	0.03f,		// 0.2
	0.05f,		// 0.3
	0.1f,		// 0.4
	1.f,		// 0.5
	25.f,		// 0.6
	75.f,		// 0.7
	150.f,		// 0.8
	1000.f,		// 0.9
	10000000.f,	// 1.0
};

static F32 costLookup(F32 target_error)
{
	F32 err = target_error * 10.f;
	int lu = err;
	F32 t = err - (F32)lu;

	F32 max_cost = cost_lookup[lu];

	assert(t >= 0 && t < 1);

	if (t)
		max_cost += t * (cost_lookup[lu+1] - cost_lookup[lu]);

	return max_cost;
}

static F32 inverseCostLookup(F32 max_cost)
{
	int i;
	F32 t, error;

	for (i = 0; i < 11; i++)
	{
		if (max_cost <= cost_lookup[i])
		{
			if (i == 0)
				return 0;
			ANALYSIS_ASSUME(i != 0);
#pragma warning(suppress:6200)		// /analyze ignoring the ANALYSIS_ASSUME above
			t = (cost_lookup[i] - max_cost) / (cost_lookup[i] - cost_lookup[i-1]);

			error = (((F32)i) - t) / 10.f;
			assert (error >= 0 && error <= 1);

			return error;
		}
	}

	return 1;
}

static F32 dist_lookup[11] = 
{
	0.0f,
	0.0f,
	25.0f,
	50.0f,
	75.f,
	100.f,
	150.f,
	200.f,
	400.f,
	800.f,
	3000.f,
};

static F32 distLookup(F32 target_error)
{
	F32 err = target_error * 10.f;
	int lu = err;
	F32 t = err - (F32)lu;

	F32 dist = dist_lookup[lu];

	assert(t >= 0 && t < 1);

	if (t)
		dist += t * (dist_lookup[lu+1] - dist_lookup[lu]);

	return dist;
}

static F32 getDistanceForError(F32 target_error, F32 radius)
{
	F32 dist = distLookup(target_error);
	if (dist < 800.f && radius > 100.f)
		dist *= radius / 100.f;
	else if (dist < 800.f && radius < 30.f)
		dist *= radius / 30.f;
	return MAX(dist, radius);
}

static void freeReduceInstruction(TriCutType *tc, ReduceInstruction *ri)
{
	eaiDestroy(&ri->vremaps.remaps);
	eaiDestroy(&ri->vremaps.remap_tris);
	eaiDestroy(&ri->vremaps.changes);
	eafDestroy(&ri->vremaps.positions);
	eafDestroy(&ri->vremaps.tex1s);
	eafDestroy(&ri->vremaps.normals);
	eaiDestroy(&ri->vremaps.varcolors);
	MP_FREE_MEMBER(tc, ReduceInstruction, ri);
}

static void freeMeshReduction(TriCutType *tc, MeshReduction *mr)
{
	int i;
	for (i = eaSize(&mr->instructions) - 1; i >= 0; --i)
		freeReduceInstruction(tc, mr->instructions[i]);
	eaDestroy(&mr->instructions);
	free(mr);
}

static void convertMRtoGMR(int varcolor_size, GMeshReductions *gmr, const MeshReduction *mr)
{
	int i, j;
	int *remaps_ptr, *remap_tris_ptr, *changes_ptr;
	Vec3 *pos_ptr, *norm_ptr;
	Vec2 *tex_ptr;
	U8 *varcolor_ptr;

	gmr->num_reductions = eaSize(&mr->instructions);

	if (!gmr->num_reductions)
		return;

	gmr->num_tris_left = malloc(gmr->num_reductions*sizeof(int));
	gmr->error_values = malloc(gmr->num_reductions*sizeof(F32));
	gmr->remaps_counts = malloc(gmr->num_reductions*sizeof(int));
	gmr->changes_counts = malloc(gmr->num_reductions*sizeof(int));

	gmr->total_remaps = 0;
	gmr->total_remap_tris = 0;
	gmr->total_changes = 0;

	for (i = 0; i < eaSize(&mr->instructions); i++)
	{
		gmr->num_tris_left[i] = mr->instructions[i]->num_tris_left;
		gmr->error_values[i] = mr->instructions[i]->error;
		gmr->remaps_counts[i] = eaiSize(&mr->instructions[i]->vremaps.remaps) / 3;
		gmr->changes_counts[i] = eaiSize(&mr->instructions[i]->vremaps.changes);

		gmr->total_remaps += gmr->remaps_counts[i];
		gmr->total_remap_tris += eaiSize(&mr->instructions[i]->vremaps.remap_tris);
		gmr->total_changes += gmr->changes_counts[i];
	}

	if (gmr->total_remaps)
	{
		gmr->remaps = malloc(3*gmr->total_remaps*sizeof(int));
	}
	if (gmr->total_remap_tris)
	{
		gmr->remap_tris = malloc(gmr->total_remap_tris*sizeof(int));
	}
	if (gmr->total_changes)
	{
		gmr->changes = malloc(gmr->total_changes*sizeof(int));
		gmr->positions = malloc(gmr->total_changes*sizeof(Vec3));
		gmr->tex1s = calloc(gmr->total_changes, sizeof(Vec2));
		gmr->normals = calloc(gmr->total_changes, sizeof(Vec3));
		gmr->varcolors = calloc(gmr->total_changes, varcolor_size * sizeof(U8));
	}

	remaps_ptr = gmr->remaps;
	remap_tris_ptr = gmr->remap_tris;

	changes_ptr = gmr->changes;
	pos_ptr = gmr->positions;
	tex_ptr = gmr->tex1s;
	norm_ptr = gmr->normals;
	varcolor_ptr = gmr->varcolors;

	for (i = 0; i < eaSize(&mr->instructions); i++)
	{
		VertRemaps *vremaps = &mr->instructions[i]->vremaps;

		memcpy(remaps_ptr, vremaps->remaps, 3*gmr->remaps_counts[i]*sizeof(int));
		remaps_ptr += 3*gmr->remaps_counts[i];

		memcpy(remap_tris_ptr, vremaps->remap_tris, eaiSize(&vremaps->remap_tris)*sizeof(int));
		remap_tris_ptr += eaiSize(&vremaps->remap_tris);

		memcpy(changes_ptr, vremaps->changes, gmr->changes_counts[i]*sizeof(int));
		changes_ptr += gmr->changes_counts[i];

		memcpy(pos_ptr, vremaps->positions, gmr->changes_counts[i]*sizeof(Vec3));
		pos_ptr += gmr->changes_counts[i];

		if (vremaps->tex1s)
		{
			memcpy(tex_ptr, vremaps->tex1s, gmr->changes_counts[i]*sizeof(Vec2));
			tex_ptr += gmr->changes_counts[i];
		}

		if (vremaps->normals)
		{
			memcpy(norm_ptr, vremaps->normals, gmr->changes_counts[i]*sizeof(Vec3));
			norm_ptr += gmr->changes_counts[i];
		}

		if (vremaps->varcolors)
		{
			for (j = 0; j < gmr->changes_counts[i]*varcolor_size; ++j)
				varcolor_ptr[j] = vremaps->varcolors[j];
			varcolor_ptr += gmr->changes_counts[i]*varcolor_size;
		}
	}

	assert(remaps_ptr == gmr->remaps + 3*gmr->total_remaps);
	assert(remap_tris_ptr == gmr->remap_tris + gmr->total_remap_tris);
	assert(changes_ptr == gmr->changes + gmr->total_changes);
	assert(pos_ptr == gmr->positions + gmr->total_changes);
}

GMeshReductions *gmeshCalculateReductions(const GMesh *mesh, F32 distances[3], F32 scale_by_area, bool use_optimal_placement, bool maintain_borders, bool is_terrain)
{
	int i, j, k, d = 0, debug_count = 0;
	TriCutType *tc;
	F32 rad, target_tripercent = 0.75f;
	GMesh meshcopy={0};
	GMeshReductions *gmr = calloc(sizeof(*gmr), 1);
	MeshReduction *mr = calloc(sizeof(*mr), 1);
	SEdge *e;

	SET_FP_CONTROL_WORD_DEFAULT;

	distances[0] = distances[1] = distances[2] = -1;

	gmeshCopy(&meshcopy, mesh, 0);
	tc = createTriCut(&meshcopy, scale_by_area, use_optimal_placement, maintain_borders, is_terrain, NULL);
	rad = distance3(tc->bounds_min, tc->bounds_max) * 0.5f;

	for (e = tcGetCheapestEdge(tc); e; e = tcGetCheapestEdge(tc))
	{
		ReduceInstruction *ri;
		ri = collapseEdge(tc, e);
		if(!ri)
			continue;
		ri->num_tris_left = tc->live_tris;
		ri->error = inverseCostLookup(ri->cost);
		if (ri->num_tris_left < target_tripercent * mesh->tri_count)
		{
			assert(d < 3);
			distances[d++] = getDistanceForError(ri->error, rad);
			target_tripercent -= 0.25f;
		}
		eaPush(&mr->instructions, ri);
		debug_count++;
	}

	gmeshFreeData(&meshcopy);

	gmeshCopy(&meshcopy, mesh, 0);
	for (j = 0; j < eaSize(&mr->instructions); j++)
	{
		ReduceInstruction *ri = mr->instructions[j];

		for (i = 0; i < eaiSize(&ri->vremaps.remaps); i+=3)
		{
			int *tri_idxs=0;
			int old_idx = ri->vremaps.remaps[i];
			int new_idx = ri->vremaps.remaps[i+1];

			assert(old_idx >= 0);
			assert(new_idx >= 0);

			if (old_idx >= meshcopy.vert_count || new_idx >= meshcopy.vert_count)
				continue;

			for (k = 0; k < meshcopy.tri_count; k++)
			{
				GTriIdx *tri = &meshcopy.tris[k];
				if (tri->idx[0] == old_idx)
				{
					eaiPushUnique(&tri_idxs, k);
					tri->idx[0] = new_idx;
				}
				if (tri->idx[1] == old_idx)
				{
					eaiPushUnique(&tri_idxs, k);
					tri->idx[1] = new_idx;
				}
				if (tri->idx[2] == old_idx)
				{
					eaiPushUnique(&tri_idxs, k);
					tri->idx[2] = new_idx;
				}
			}

			ri->vremaps.remaps[i+2] = eaiSize(&tri_idxs);
			for (k = 0; k < eaiSize(&tri_idxs); k++)
				eaiPush(&ri->vremaps.remap_tris, tri_idxs[k]);
			eaiDestroy(&tri_idxs);
		}
	}
	gmeshFreeData(&meshcopy);

	convertMRtoGMR(mesh->varcolor_size, gmr, mr);

	freeMeshReduction(tc, mr);

	freeTriCut(tc);

	return gmr;
}

int simplygonEnabled = 0;
AUTO_CMD_INT(simplygonEnabled, simplygonEnabled) ACMD_CMDLINE;

static CRITICAL_SECTION simplygonCS;

// Use the official SG build for now.  Make sure to set the include in simplygonsdkloader.h and SimplygonInterface.cpp to point to the appropriate simplygon header file.
#define USE_STANDARD_SIMPLYGON_BUILD 1

#if USE_STANDARD_SIMPLYGON_BUILD
#ifdef _WIN64
	#define SIMPLYGON_SDK_DLL SIMPLYGON_STANDARD_SDK_DLL_X64
#else
	#define SIMPLYGON_SDK_DLL SIMPLYGON_STANDARD_SDK_DLL_WIN32
#endif

#else

#ifdef _WIN64
#define SIMPLYGON_SDK_DLL SIMPLYGON_CRYPTICNW_SDK_DLL_X64
#else
#define SIMPLYGON_SDK_DLL SIMPLYGON_CRYPTICNW_SDK_DLL_WIN32
#endif

#endif

void simplygonSetupPath(void) {
	simplygon_setSimplygonPath(SIMPLYGON_SDK_DLL);
}

AUTO_RUN;
void simplygonInit(void) {
	InitializeCriticalSection(&simplygonCS);
	simplygon_setSimplygonCallbacks(
		simplygonSetupPath,
		loadstart_printf,
		loadend_printf,
		loadupdate_printf);
}

void simplygonSetEnabled(bool enabled) {
	simplygonEnabled = enabled;
}

bool simplygonGetEnabled(void) {
	return !isProductionMode() && simplygonEnabled;
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void simplygonSetOutputToConsole(bool outputToConsole) {
	simplygon_setOutputToConsole(outputToConsole);
}

AUTO_COMMAND ACMD_CATEGORY(Debug);
void simplygonDumpRecentLogs(void) {
	simplygon_dumpRecentLogs();
}

SimplygonMesh *simplygonMeshFromGMesh(const GMesh *src, int *texIdMappings, int numTexIdMappings, bool *overSizeLimit, const char *name, int usageBitMask) {

	SimplygonMesh *mesh;
	unsigned int *scratchIndexes;
	unsigned int *scratchTexIds;
	float *scratchNormals;
	float *scratchNormals2;
	float *scratchTexCoords0;
	float *scratchTexCoords1;
	float *scratchColors;
	float *scratchBinormals = NULL;
	float *scratchTangents = NULL;
	int *texIdToRealMaterialId = NULL;

	int i;
	int j;

	GMeshAttributeUsage usagebits = src->usagebits & usageBitMask;

	GMeshAttributeUsage supportedBits = (USE_POSITIONS | USE_POSITIONS2 |
										USE_TEX1S     | USE_TEX2S |
										USE_NORMALS   | USE_NORMALS2 |
										USE_COLORS    | USE_BONEWEIGHTS |
										USE_BINORMALS | USE_TANGENTS |
										USE_OPTIONAL_PARSE_STRUCT);

	if((usagebits & supportedBits) != usagebits) {
		log_printf(LOG_ERRORS, "Unsupported source attributes in simplygonMeshFromGMesh, 0x%x", usagebits & supportedBits);
	}

	if(src->tri_count == 0 || src->vert_count == 0) {
		log_printf(LOG_ERRORS, "Supplied mesh contains no geometry for simplygon to process.");
		// I don't know what to do with this. It's not any kind of model that's going to go nicely into Simplygon.
		return NULL;
	}

	EnterCriticalSection(&simplygonCS);

	simplygonSetupPath();

	if(!simplygon_interfaceTest()) {

		// Simplygon just didn't load. Probably no license key or DLL present.
		LeaveCriticalSection(&simplygonCS);
		return false;
	}

	mesh = simplygon_createMesh(name);

	// FIXME: Lots of needless allocating here.
	scratchIndexes = calloc(sizeof(unsigned int), src->tri_count * 3);
	scratchTexIds  = calloc(sizeof(unsigned int), src->tri_count);
	scratchNormals = calloc(sizeof(float), src->tri_count * 3 * 3);
	scratchNormals2 = calloc(sizeof(float), src->tri_count * 3 * 3);
	scratchTexCoords0 = calloc(sizeof(float), src->tri_count * 3 * 2);
	scratchTexCoords1 = calloc(sizeof(float), src->tri_count * 3 * 2);
	scratchColors = calloc(sizeof(float), src->tri_count * 3 * 4);
	if((usagebits & (USE_BINORMALS | USE_TANGENTS)) == (USE_BINORMALS | USE_TANGENTS)) {
		scratchBinormals = calloc(sizeof(float), src->tri_count * 3 * 3);
		scratchTangents = calloc(sizeof(float), src->tri_count * 3 * 3);
	}

	{
		bool didChange = true;

		for(i = 0; i < src->tri_count; i++) {
			ea32PushUnique(&texIdToRealMaterialId, src->tris[i].tex_id);
		}

		// Ugly bubblesort.
		// FIXME: Should probably use ea32Qsort here.
		while(didChange) {
			didChange = false;
			for(i = 0; i < ea32Size(&texIdToRealMaterialId) - 1; i++) {
				if(texIdToRealMaterialId[i] > texIdToRealMaterialId[i+1]) {
					ea32Swap(&texIdToRealMaterialId, i, i+1);
					didChange = true;
				}
			}
		}

	}

	for(i = 0; i < src->tri_count; i++) {

		// Copy over faces

		scratchIndexes[i*3]   = src->tris[i].idx[0];
		scratchIndexes[i*3+1] = src->tris[i].idx[1];
		scratchIndexes[i*3+2] = src->tris[i].idx[2];

		if(texIdMappings) {
			int texId = ea32Find(&texIdToRealMaterialId, src->tris[i].tex_id);
			if(texId < numTexIdMappings && texId >= 0) {
				scratchTexIds[i] = texIdMappings[texId];
			} else {
				scratchTexIds[i] = 0;
			}

		} else {
			scratchTexIds[i] = src->tris[i].tex_id;
		}

		// Convert per-triangle-corner data like normals and texture coordinates.

		for(j = 0; j < 3; j++) {
			int vertIndex = src->tris[i].idx[j];

			if(usagebits & USE_NORMALS) {
				scratchNormals[(i*3*3)+(j*3)+0] = src->normals[vertIndex][0];
				scratchNormals[(i*3*3)+(j*3)+1] = src->normals[vertIndex][1];
				scratchNormals[(i*3*3)+(j*3)+2] = src->normals[vertIndex][2];
			}

			if((usagebits & (USE_BINORMALS | USE_TANGENTS)) == (USE_BINORMALS | USE_TANGENTS)) {
				scratchBinormals[(i*3*3)+(j*3)+0] = src->binormals[vertIndex][0];
				scratchBinormals[(i*3*3)+(j*3)+1] = src->binormals[vertIndex][1];
				scratchBinormals[(i*3*3)+(j*3)+2] = src->binormals[vertIndex][2];
				scratchTangents[(i*3*3)+(j*3)+0] = src->tangents[vertIndex][0];
				scratchTangents[(i*3*3)+(j*3)+1] = src->tangents[vertIndex][1];
				scratchTangents[(i*3*3)+(j*3)+2] = src->tangents[vertIndex][2];
			}

			if(usagebits & USE_NORMALS2) {
				scratchNormals2[(i*3*3)+(j*3)+0] = src->normals2[vertIndex][0];
				scratchNormals2[(i*3*3)+(j*3)+1] = src->normals2[vertIndex][1];
				scratchNormals2[(i*3*3)+(j*3)+2] = src->normals2[vertIndex][2];
			}

			if(usagebits & USE_TEX1S) {
				scratchTexCoords0[(i*3*2) + (j*2) + 0] = src->tex1s[vertIndex][0];
				scratchTexCoords0[(i*3*2) + (j*2) + 1] = src->tex1s[vertIndex][1];
			}

			if(usagebits & USE_TEX2S) {
				scratchTexCoords1[(i*3*2) + (j*2) + 0] = src->tex2s[vertIndex][0];
				scratchTexCoords1[(i*3*2) + (j*2) + 1] = src->tex2s[vertIndex][1];
			}

			if(usagebits & USE_COLORS) {
				scratchColors[(i*3*4) + (j*4) + 0] = (float)(src->colors[vertIndex].r) / 255.0;
				scratchColors[(i*3*4) + (j*4) + 1] = (float)(src->colors[vertIndex].g) / 255.0;
				scratchColors[(i*3*4) + (j*4) + 2] = (float)(src->colors[vertIndex].b) / 255.0;
				scratchColors[(i*3*4) + (j*4) + 3] = (float)(src->colors[vertIndex].a) / 255.0;
			}
		}

	}

	simplygon_setMeshNumVertices(mesh, src->vert_count);

	// Copy over fields that don't require any conversion to per-triangle or per-triangle corner stuff.

	if(usagebits & USE_POSITIONS) {

		simplygon_setMeshVertexPositions(
			mesh,
			(float*)src->positions,
			0);

		if(overSizeLimit) {
			int n;
			Vec3 min = {  9999.99,  9999.99,  9999.99 };
			Vec3 max = { -9999.99, -9999.99, -9999.99 };
			float sizeLimit = 1000.0;

			for(n = 0; n < src->vert_count; n++) {
				int k;
				for(k = 0; k < 3; k++) {
					if(src->positions[n][k] < min[k]) {
						min[k] = src->positions[n][k];
					}
					if(src->positions[n][k] > max[k]) {
						max[k] = src->positions[n][k];
					}
				}
			}

			if(min[0] < -sizeLimit || max[0] > sizeLimit ||
			   min[1] < -sizeLimit || max[1] > sizeLimit ||
			   min[2] < -sizeLimit || max[2] > sizeLimit) {

				// Model's possibly too big for Simplygon?
				*overSizeLimit = true;
			}
		}

	}

	if(usagebits & USE_POSITIONS2) {
		simplygon_setMeshVertexPositions(
			mesh,
			(float*)src->positions2,
			1);
	}

	if(usagebits & USE_BONEWEIGHTS) {
		simplygon_setMeshBoneData(
			mesh,
			&(src->bonemats[0][0]),
			&(src->boneweights[0][0]));
	}

	// Now do per-triangle-corner stuff.

	simplygon_setMeshNumTriangles(mesh, src->tri_count);

	simplygon_setMeshTriangles(
		mesh,
		scratchIndexes,
		scratchTexIds);

	if(usagebits & USE_NORMALS) {
		simplygon_setMeshVertexNormals(
			mesh,
			scratchNormals,
			0);
	}

	if(usagebits & USE_NORMALS2) {
		simplygon_setMeshVertexNormals(
			mesh,
			scratchNormals2,
			1);
	}

	if((usagebits & (USE_TANGENTS | USE_BINORMALS)) == (USE_TANGENTS | USE_BINORMALS)) {
		simplygon_setMeshVertexTangentSpace(
			mesh,
			scratchTangents,
			scratchBinormals);
	}

	if(usagebits & USE_TEX1S) {
		simplygon_setMeshVertexTexCoords(
			mesh,
			scratchTexCoords0,
			0);
	}

	if(usagebits & USE_TEX2S) {
		simplygon_setMeshVertexTexCoords(
			mesh,
			scratchTexCoords1,
			1);
	}

	if(usagebits & USE_COLORS) {
		simplygon_setMeshVertexDiffuseColors(
			mesh,
			scratchColors);
	}

	free(scratchTexCoords1);
	free(scratchTexCoords0);
	free(scratchNormals);
	free(scratchNormals2);
	free(scratchTexIds);
	free(scratchIndexes);
	free(scratchColors);

	if((usagebits & (USE_BINORMALS | USE_TANGENTS)) == (USE_BINORMALS | USE_TANGENTS)) {
		free(scratchTangents);
		free(scratchBinormals);
	}

	ea32Destroy(&texIdMappings);

	LeaveCriticalSection(&simplygonCS);

	return mesh;
}

bool simplygonMeshToGMesh(SimplygonMesh *mesh, GMesh *dst, int usageBitMask) {

	unsigned int *scratchIndexes = NULL;
	unsigned int *scratchTexIds = NULL;
	float *scratchNormals = NULL;
	float *scratchNormals2 = NULL;
	float *scratchTexCoords0 = NULL;
	float *scratchTexCoords1 = NULL;
	float *scratchPositions = NULL;
	float *scratchPositions2 = NULL;
	float *scratchColors = NULL;
	float *scratchTangents = NULL;
	float *scratchBinormals = NULL;

	float *scratchBoneweights = NULL;
	unsigned short *scratchBoneIds = NULL;

	int i;
	int j;

	int usageBits;

	EnterCriticalSection(&simplygonCS);

	simplygonSetupPath();

	usageBits = usageBitMask & simplygon_getMeshUsageBits(mesh);

	gmeshFreeData(dst);
	ZeroStructForce(dst);
	gmeshSetUsageBits(dst, usageBits);

	{

		unsigned int numSGVerts = simplygon_getMeshNumVertices(mesh);
		unsigned int numSGTris  = simplygon_getMeshNumTriangles(mesh);

		if(!numSGTris || !numSGVerts) {

			Errorf(
				"Simplygon failed on mesh %s because the result had no triangles or vertices.",
				simplygon_getMeshName(mesh));

			LeaveCriticalSection(&simplygonCS);
			return false;
		}

		assert(numSGVerts);
		assert(numSGTris);

		scratchTexIds = calloc(sizeof(unsigned int), numSGTris);
		scratchIndexes = calloc(sizeof(unsigned int) * 3, numSGTris);
		simplygon_getMeshTriangles(mesh, scratchIndexes, scratchTexIds);

		if(usageBits & USE_POSITIONS) {
			scratchPositions = calloc(sizeof(float) * 3, numSGVerts);
			simplygon_getMeshVertexPositions(mesh, scratchPositions, 0);
		} else {
			scratchPositions = NULL;
		}

		if(usageBits & USE_POSITIONS2) {
			scratchPositions2 = calloc(sizeof(float) * 3, numSGVerts);
			simplygon_getMeshVertexPositions(mesh, scratchPositions2, 1);
		} else {
			scratchPositions2 = NULL;
		}

		if(usageBits & USE_NORMALS) {
			scratchNormals = calloc(sizeof(float) * 3 * 3, numSGTris);
			simplygon_getMeshVertexNormals(mesh, scratchNormals, 0);
		} else {
			scratchNormals = NULL;
		}

		if((usageBits & (USE_BINORMALS | USE_TANGENTS)) == (USE_BINORMALS | USE_TANGENTS)) {
			scratchTangents = ScratchAlloc(sizeof(float) * 3 * 3 * numSGTris);
			scratchBinormals = ScratchAlloc(sizeof(float) * 3 * 3* numSGTris);
			simplygon_getMeshVertexTangentSpace(mesh, scratchTangents, scratchBinormals);
		} else {
			scratchBinormals = NULL;
			scratchTangents = NULL;
		}

		if(usageBits & USE_NORMALS2) {
			scratchNormals2 = calloc(sizeof(float) * 3 * 3, numSGTris);
			simplygon_getMeshVertexNormals(mesh, scratchNormals2, 1);
		} else {
			scratchNormals2 = NULL;
		}

		if(usageBits & USE_TEX1S) {
			scratchTexCoords0 = calloc(sizeof(float) * 3 * 2, numSGTris);
			simplygon_getMeshVertexTexCoords(mesh, scratchTexCoords0, 0);
		} else {
			scratchTexCoords0 = NULL;
		}

		if(usageBits & USE_TEX2S) {
			scratchTexCoords1 = calloc(sizeof(float) * 3 * 2, numSGTris);
			simplygon_getMeshVertexTexCoords(mesh, scratchTexCoords1, 1);
		} else {
			scratchTexCoords1 = NULL;
		}

		if(usageBits & USE_COLORS) {
			scratchColors = calloc(sizeof(float) * 3 * 4, numSGTris);
			simplygon_getMeshVertexDiffuseColors(mesh, scratchColors);
		} else {
			scratchColors = NULL;
		}

		if(usageBits & USE_BONEWEIGHTS) {
			scratchBoneweights = calloc(sizeof(float) * 4, numSGVerts);
			scratchBoneIds = calloc(sizeof(unsigned short) * 4, numSGVerts);
			simplygon_getMeshBoneData(
				mesh,
				scratchBoneIds,
				scratchBoneweights);
		} else {
			scratchBoneweights = NULL;
			scratchBoneIds = NULL;
		}

		for(i = 0; i < (int)numSGTris; i++) {

			unsigned int realIndexes[3];

			for(j = 0; j < 3; j++) {

				// fakeIndex is a vertex index in the Simplygon mesh. But we're going to remap these as we add vertices
				// to the GMesh. Some fields (like position) use this directly while others are stored per triangle
				// corner (like normals).
				unsigned int fakeIndex = scratchIndexes[i * 3 + j];

				Color color;
				if(usageBits & USE_COLORS) {
					color.r = (U8)(scratchColors[i*3*4 + j*4+0] * 255.0);
					color.g = (U8)(scratchColors[i*3*4 + j*4+1] * 255.0);
					color.b = (U8)(scratchColors[i*3*4 + j*4+2] * 255.0);
					color.a = (U8)(scratchColors[i*3*4 + j*4+3] * 255.0);
				}

				realIndexes[j] = gmeshAddVert(
					dst,
					(usageBits & USE_POSITIONS) ? scratchPositions + (fakeIndex * 3) : NULL, // pos
					(usageBits & USE_POSITIONS2) ? scratchPositions2 + (fakeIndex * 3) : NULL, // pos2
					(usageBits & USE_NORMALS) ? (scratchNormals + (i * 3 * 3 + j * 3)) : NULL, // norm
					(usageBits & USE_NORMALS2) ? (scratchNormals2 + (i * 3 * 3 + j * 3)) : NULL, // norm2

					(usageBits & USE_BINORMALS) ? (scratchBinormals + (i * 3 * 3 + j * 3)) : NULL, // binorm
					(usageBits & USE_TANGENTS) ? (scratchTangents + (i * 3 * 3 + j * 3)) : NULL, // tangent

					(usageBits & USE_TEX1S) ? (scratchTexCoords0 + (i * 3 * 2 + j * 2)) : NULL, // tex1
					(usageBits & USE_TEX2S) ? (scratchTexCoords1 + (i * 3 * 2 + j * 2)) : NULL, // tex2
					(usageBits & USE_COLORS) ? &color : NULL, // color
					NULL, // varcolor
					(usageBits & USE_BONEWEIGHTS) ? (scratchBoneweights + fakeIndex * 4) : NULL, // boneweight
					(usageBits & USE_BONEWEIGHTS) ? (scratchBoneIds + fakeIndex * 4) : NULL, // boneid
					true, false, false);
			}

			gmeshAddTri(
				dst,
				realIndexes[0],
				realIndexes[1],
				realIndexes[2],
				scratchTexIds[i],
				true);
		}

		free(scratchBoneweights);
		free(scratchBoneIds);
		free(scratchTexCoords0);
		free(scratchTexCoords1);
		free(scratchTexIds);
		free(scratchIndexes);
		free(scratchPositions);
		free(scratchPositions2);
		free(scratchNormals);
		free(scratchColors);
		ScratchFree(scratchBinormals);
		ScratchFree(scratchTangents);

	}

	gmeshSort(dst);

	LeaveCriticalSection(&simplygonCS);

	// If Simplygon completely destroyed a model, we don't want that. Fall back on internal mesh reduction stuff.
	return !!(dst->tri_count);
}

float simplygonErrorScale = 1.0f;
AUTO_CMD_FLOAT(simplygonErrorScale, simplygonErrorScale) ACMD_CMDLINE;

int simplygonErrorUseRadius = 1;
AUTO_CMD_INT(simplygonErrorUseRadius, simplygonErrorUseRadius) ACMD_CMDLINE;

static float getGmeshRadius(const GMesh *src) {

	Vec3 min = {FLT_MAX, FLT_MAX, FLT_MAX};
	Vec3 max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
	int i;
	int j;

	for(i = 0; i < src->vert_count; i++) {
		for(j = 0; j < 3; j++) {
			min[j] = MIN(src->positions[i][j], min[j]);
			max[j] = MAX(src->positions[i][j], max[j]);
		}
	}

	return MAX(MAX((max[0] - min[0]), (max[1] - min[1])), (max[2] - min[2])) / 2.0;
}

bool simplygonReduce(GMesh *dst, const GMesh *src, float maxError, float targetTriCount, float *actualError) {

	int supportedBits = (USE_POSITIONS | USE_POSITIONS2 | USE_TEX1S | USE_TEX2S | USE_NORMALS | USE_NORMALS2 | USE_COLORS | USE_BONEWEIGHTS);
	SimplygonMesh *mesh;
	bool probablyOversized = false;
	int retries = 5;

	if(simplygonErrorUseRadius) {
		Vec3 min = {FLT_MAX, FLT_MAX, FLT_MAX};
		Vec3 max = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
		Vec3 mid;
		int i;
		int j;
		float radius;

		for(i = 0; i < src->vert_count; i++) {
			for(j = 0; j < 3; j++) {
				min[j] = MIN(src->positions[i][j], min[j]);
				max[j] = MAX(src->positions[i][j], max[j]);
			}
		}
		lerpVec3(min, 0.5f, max, mid);
		radius = MAX(MAX((max[0] - min[0]), (max[1] - min[1])), (max[2] - min[2]));

		maxError *= radius;
	}

	maxError *= simplygonErrorScale;

	do {

		mesh = simplygonMeshFromGMesh(src, NULL, 0, &probablyOversized, "gmesh", ~0);

		if(probablyOversized) {

			// Too big?
			if(mesh) {
				simplygon_destroyMesh(mesh);
			}

			return false;
		}

		if(!mesh) {

			// Something failed in the conversion to a Simplygon mesh.
			return false;
		}

		EnterCriticalSection(&simplygonCS);
		*actualError = simplygon_reduceMesh(
			mesh, maxError, targetTriCount);
		LeaveCriticalSection(&simplygonCS);

		if(!simplygonMeshToGMesh(mesh, dst, ~0)) {

			// Conversion back failed.
			simplygon_destroyMesh(mesh);
			mesh = NULL;
		}

		if(!mesh) {

			if(retries) {

				// Sometimes Simplygon just spits out a mdoel with no verts or triangles. We can't handle that case, so
				// just increase the triangle account and try again.
				retries--;
				targetTriCount++;

			} else {

				return false;

			}
		}

	} while(!mesh);


	assert(simplygon_getMeshUsageBits(mesh) == src->usagebits);
	simplygon_destroyMesh(mesh);
	return true;

}

F32 gmeshReduce_dbg(
	GMesh *dst, const GMesh *src,
	F32 target_error, F32 target_tricount,
	ReductionMethod method, F32 scale_by_area,
	F32 upscale, bool use_optimal_placement,
	bool maintain_borders, bool is_terrain,
	bool disable_simplygon,
	const char *debug_name,
	const char *error_filename MEM_DBG_PARMS)
{

	GMesh reduced_mesh = {0};
	int debug_count = 0;
	TriCutType *tc;
	SEdge *e;
	F32 error = 0;
	Vec3 *texmap = NULL;
	int texmap_res = 256;
	F32 one_over_texmap_step = 1.f;
	U32 default_fpcw, final_fpcw;

	if(src->varcolor_size > 20) {
		if(is_terrain) {
			ErrorFilenamef(
				error_filename,
				"Too many materials on terrain in section %s. Returning empty model from gmeshReduce!",
				debug_name);
		} else {
			ErrorFilenamef(
				error_filename,
				"Too many varcolors in model %s. Returning empty model from gmeshReduce!",
				debug_name);
		}
		if(dst) {
			gmeshFreeData(dst);
			ZeroStructForce(dst);
		}
		return 1.0f;
	}


	SET_FP_CONTROL_WORD_DEFAULT;
	_controlfp_s(&default_fpcw, 0, 0);

	if (target_error > 1)
		target_error = 1;
	if (target_error < 0)
		target_error = 0;

	// Just let Simplygon handle this, if possible.
	if(!is_terrain && simplygonGetEnabled() && !disable_simplygon) {

		FP_NO_EXCEPTIONS_BEGIN;

		simplygonSetupPath();

		if(simplygon_interfaceTest()) {

			float actualError = target_error;
			if(simplygonReduce(dst, src, target_error, target_tricount, &actualError)) {
				return actualError;
			} else {
				if(debug_name) {
					ErrorFilenamef(debug_name, "Simplygon failed to reduce a mesh: %s", debug_name);
				}
			}

		} else {
			char *simplygonDump = simplygon_constructLogDump();
			if (simplygonDump)
				Errorf("Simplygon is enabled, but the library failed to load. Disabling Simplygon support. Falling back on internal mesh reduction. Simplygon Log Dump : %s", simplygonDump);
			else
				Errorf("Simplygon is enabled, but the library failed to load. Disabling Simplygon support. Falling back on internal mesh reduction.");
			simplygonEnabled = 0;
		}

		FP_NO_EXCEPTIONS_END;

	}

	if (dst == src)
	{
		reduced_mesh = *src;
		ZeroStructForce(dst);
	}
	else
	{
		gmeshCopy(&reduced_mesh, src, false);
	}

	if (is_terrain)
	{
		int i, x, y;
		F32 max_size = 1, texmap_step;

		for (i = 0; i < reduced_mesh.vert_count; ++i)
		{
			MAX1(max_size, reduced_mesh.positions[i][0]);
			MAX1(max_size, reduced_mesh.positions[i][2]);
		}

		texmap_step = ceilf(max_size / texmap_res);
		one_over_texmap_step = 1.f / texmap_step;

		texmap = ScratchAlloc(texmap_res * texmap_res * sizeof(Vec3));

		// generate map of texture mapping directions
		for (i = 0; i < reduced_mesh.tri_count; ++i)
		{
			Vec2 p0, p1, p2;
			IVec2 trimin, trimax;

			setVec2(p0, reduced_mesh.positions[reduced_mesh.tris[i].idx[0]][0] * one_over_texmap_step, reduced_mesh.positions[reduced_mesh.tris[i].idx[0]][2] * one_over_texmap_step);
			setVec2(p1, reduced_mesh.positions[reduced_mesh.tris[i].idx[1]][0] * one_over_texmap_step, reduced_mesh.positions[reduced_mesh.tris[i].idx[1]][2] * one_over_texmap_step);
			setVec2(p2, reduced_mesh.positions[reduced_mesh.tris[i].idx[2]][0] * one_over_texmap_step, reduced_mesh.positions[reduced_mesh.tris[i].idx[2]][2] * one_over_texmap_step);
			assert(reduced_mesh.tris[i].tex_id < 3);

			trimin[0] = imin3(p0[0], p1[0], p2[0]);
			trimin[0] = CLAMP(trimin[0], 0, texmap_res-1);

			trimin[1] = imin3(p0[1], p1[1], p2[1]);
			trimin[1] = CLAMP(trimin[1], 0, texmap_res-1);

			trimax[0] = imax3(p0[0], p1[0], p2[0]);
			trimax[0] = CLAMP(trimax[0], 0, texmap_res-1);

			trimax[1] = imax3(p0[1], p1[1], p2[1]);
			trimax[1] = CLAMP(trimax[1], 0, texmap_res-1);

			// loop over part of texmap that overlaps the triangle
			for (y = trimin[1]; y <= trimax[1]; ++y)
			{
				for (x = trimin[0]; x <= trimax[0]; ++x)
				{
					Vec2 clip_min, clip_max;
					setVec2(clip_min, x, y);
					addVec2same(clip_min, 1, clip_max);
					texmap[y * texmap_res + x][reduced_mesh.tris[i].tex_id] += findClippedArea2D(p0, p1, p2, clip_min, clip_max);
				}
			}

			reduced_mesh.tris[i].tex_id = 0;
		}
	}

	tc = createTriCut(&reduced_mesh, scale_by_area, use_optimal_placement, maintain_borders, is_terrain, debug_name);
	tc->target_error = target_error;

	for (e = tcGetCheapestEdge(tc); e; e = tcGetCheapestEdge(tc))
	{
		ReduceInstruction *ri = NULL;
		
		error = inverseCostLookup(e->cost);

		if (method == TRICOUNT_RMETHOD && tc->live_tris <= target_tricount)
			break;
		else if (method == ERROR_RMETHOD && error > target_error)
			break;
		else if (method == TRICOUNT_AND_ERROR_RMETHOD && error > target_error && tc->live_tris <= target_tricount)
			break;

		ri = collapseEdge(tc, e);
		if(ri)
			freeReduceInstruction(tc, ri);
		debug_count++;
	}

	if (upscale)
	{
		Vec3 *normals = ScratchAlloc(reduced_mesh.vert_count * sizeof(Vec3));
		int *redirects = ScratchAlloc(reduced_mesh.vert_count * sizeof(int));
		int i, j;

		for (i = 0; i < reduced_mesh.vert_count; i++)
		{
			for (j = 0; j < i; j++)
			{
				if (nearSameVec3Tol(reduced_mesh.positions[i], reduced_mesh.positions[j], 0.0005f))
					break;
			}
			redirects[i] = j;
		}

		for (i = 0; i < reduced_mesh.tri_count; ++i)
		{
			Vec3 normal;
			GTriIdx *meshtri = &reduced_mesh.tris[i];
			if (meshtri->idx[0] < 0 || meshtri->idx[1] < 0 || meshtri->idx[2] < 0)
				continue;
			makePlaneNormal(reduced_mesh.positions[meshtri->idx[0]], reduced_mesh.positions[meshtri->idx[1]], reduced_mesh.positions[meshtri->idx[2]], normal);
			addVec3(normal, normals[redirects[meshtri->idx[0]]], normals[redirects[meshtri->idx[0]]]);
			addVec3(normal, normals[redirects[meshtri->idx[1]]], normals[redirects[meshtri->idx[1]]]);
			addVec3(normal, normals[redirects[meshtri->idx[2]]], normals[redirects[meshtri->idx[2]]]);
		}
		for (i = 0; i < reduced_mesh.vert_count; ++i)
		{
			if (maintain_borders)
			{
				for (j = 0; j < eaSize(&tc->ivertinfos[i].iedges); ++j)
				{
					e = tc->ivertinfos[i].iedges[j]->sedge;
					if (eaSize(&e->iedges) == 1 && eaiSize(&e->faces) == 1)
						break;
				}
				if (j < eaSize(&tc->ivertinfos[i].iedges))
					continue;
			}

			normalVec3(normals[redirects[i]]);
			scaleAddVec3(normals[redirects[i]], upscale, reduced_mesh.positions[i], reduced_mesh.positions[i]);
		}

		ScratchFree(redirects);
		ScratchFree(normals);
	}

	if (dst != src)
		gmeshFreeData(dst);

	if (is_terrain)
	{
		// re-texture and re-normal
		applyTerrainTexturingAndNormals(tc, dst, texmap, texmap_res, one_over_texmap_step MEM_DBG_PARMS_CALL);
		ScratchFree(texmap);
	}
	else
	{
		gmeshMerge_dbg(dst, &reduced_mesh, true, false, false, false MEM_DBG_PARMS_CALL);
	}

	LOG("Final tri count: %d", dst->tri_count);

	freeTriCut(tc);

	polyGridFree(&dst->grid);
	gmeshFreeData(&reduced_mesh);
	gmeshSort(dst);

	_controlfp_s(&final_fpcw, 0, 0);
	assert(default_fpcw == final_fpcw);

	if (ENABLE_MESH_LOGGING)
	{
		GMeshParsed *mesh_parsed = gmeshToParsedFormat(dst);
		char path[MAX_PATH];
		if (debug_name)
			sprintf(path, "%s/reduce_post_%s.msh", fileTempDir(), debug_name);
		else
			sprintf(path, "%s/reduce_post_d%03d.msh", fileTempDir(), tricut_counter);
		ParserWriteTextFile(path, parse_GMeshParsed, mesh_parsed, 0, 0);
		StructDestroy(parse_GMeshParsed, mesh_parsed);
	}

	return error;
}

