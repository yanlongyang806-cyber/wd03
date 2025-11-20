#include "GenericMeshPrivate.h"
#include "qsortG.h"
#include "ScratchStack.h"
#include "Color.h"
#include "GlobalTypes.h"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_Geometry_Art););

AUTO_RUN_ANON(memBudgetAddStructMappingIfNotMapped("SEdge", __FILE__););
AUTO_RUN_ANON(memBudgetAddStructMappingIfNotMapped("IEdge", __FILE__););
AUTO_RUN_ANON(memBudgetAddStructMappingIfNotMapped("SVertInfo", __FILE__););
AUTO_RUN_ANON(memBudgetAddStructMappingIfNotMapped("ReduceInstruction", __FILE__););

static const SVec zerosvec = {0};

//////////////////////////////////////////////////////////////////////////

static void addTriRemap(VertRemaps *vremaps, int oldidx, int newidx)
{
	eaiPush(&vremaps->remaps, oldidx);
	eaiPush(&vremaps->remaps, newidx);
	eaiPush(&vremaps->remaps, 0);
}

static void addVertChange(TriCutType *tc, VertRemaps *vremaps, int vertidx, const Vec3 newpos, const Vec2 newtex1, const Vec3 newnorm, const U8 *newvarcolor)
{
	eaiPush(&vremaps->changes, vertidx);
	eafPush3(&vremaps->positions, newpos);
	if (newtex1)
	{
		eafPush2(&vremaps->tex1s, newtex1);
	}
	if (newnorm)
	{
		eafPush3(&vremaps->normals, newnorm);
	}
	if (newvarcolor)
	{
		int i;
		for (i = 0; i < tc->mesh->varcolor_size; ++i)
		{
			eaiPush(&vremaps->varcolors, newvarcolor[i]);
		}
	}
}

__forceinline static int remapTriVert(GTriIdx *tri, int dead_vert, int new_vert)
{
	int i, ret = 0;

	for (i = 0; i < 3; i++)
	{
		if (tri->idx[i] == dead_vert)
			tri->idx[i] = new_vert;
		else if (tri->idx[i] == new_vert)
			ret = 1;
	}

	return ret;
}

static void killTri(TriCutType *tc, int idx)
{
	GTriIdx *tri = &tc->mesh->tris[idx];
	DO_DEBUG_ASSERT(!tc->triinfos[idx].is_dead);
	tc->triinfos[idx].is_dead = 1;
	--tc->live_tris;
	if (tc->triinfos[idx].sedges[0])
		eaiFindAndRemoveFastInline(&tc->triinfos[idx].sedges[0]->faces, idx);
	if (tc->triinfos[idx].sedges[1])
		eaiFindAndRemoveFastInline(&tc->triinfos[idx].sedges[1]->faces, idx);
	if (tc->triinfos[idx].sedges[2])
		eaiFindAndRemoveFastInline(&tc->triinfos[idx].sedges[2]->faces, idx);
	tc->triinfos[idx].sedges[0] = tc->triinfos[idx].sedges[1] = tc->triinfos[idx].sedges[2] = 0;
	eaiFindAndRemoveFastInline(&tc->ivertinfos[tri->idx[0]].svert->faces, idx);
	eaiFindAndRemoveFastInline(&tc->ivertinfos[tri->idx[1]].svert->faces, idx);
	eaiFindAndRemoveFastInline(&tc->ivertinfos[tri->idx[2]].svert->faces, idx);
	gmeshMarkBadTri(tc->mesh, idx);
}

bool edgeIsBoundary(TriCutType *tc, SEdge *e)
{
	return (eaSize(&e->iedges) == 1 && eaiSize(&e->faces) == 1);
}

bool edgeIsCrease(TriCutType *tc, SEdge *e)
{
	// check the angle between tris connected to this edge
	if (eaiSize(&e->faces) == 2)
	{
		int idx1 = e->faces[0];
		int idx2 = e->faces[1];

		// mark as boundary if there is a sharp angle difference between tris
		if (dotVec3(tc->triinfos[idx1].face_normal, tc->triinfos[idx2].face_normal) < BOUNDARY_ANGLE_CUTOFF)
			return true;
	}
	return false;
}

//////////////////////////////////////////////////////////////////////////

static void insertionSortSEdges(SEdge **edge_array, U32 *cost_array, int n)
{
	int i, j, highest_changed = -1, lowest_changed = n;
	SEdge *t;
	U32 tcost;

	for (i = 1; i < n; i++)
	{
		for (j = i; j > 0 && cost_array[j-1] > cost_array[j]; --j)
		{
			tcost = cost_array[j];
			cost_array[j] = cost_array[j-1];
			cost_array[j-1] = tcost;

			t = edge_array[j];
			edge_array[j] = edge_array[j-1];
			edge_array[j-1] = t;

			MAX1(highest_changed, j);
			MIN1(lowest_changed, j-1);
		}
	}

	for (j = lowest_changed; j <= highest_changed; ++j)
		edge_array[j]->array_index = j;
}

static void insertionSortSingleSEdge(SEdge **edge_array, U32 *cost_array, int n, int idx)
{
	int j, highest_changed = -1, lowest_changed = n;
	bool sort_forward = false;
	SEdge *t;
	U32 tcost;

	if (idx == 0 || cost_array[idx-1] < cost_array[idx])
		sort_forward = true;
// 	else if (idx == n-1 || cost_array[idx+1] < cost_array[idx])
// 		sort_forward = false;

	if (sort_forward)
	{
		for (j = idx; j < n-1 && cost_array[j] > cost_array[j+1]; ++j)
		{
			tcost = cost_array[j];
			cost_array[j] = cost_array[j+1];
			cost_array[j+1] = tcost;

			t = edge_array[j];
			edge_array[j] = edge_array[j+1];
			edge_array[j+1] = t;

			MAX1(highest_changed, j+1);
			MIN1(lowest_changed, j);
		}
	}
	else
	{
		for (j = idx; j > 0 && cost_array[j-1] > cost_array[j]; --j)
		{
			tcost = cost_array[j];
			cost_array[j] = cost_array[j-1];
			cost_array[j-1] = tcost;

			t = edge_array[j];
			edge_array[j] = edge_array[j-1];
			edge_array[j-1] = t;

			MAX1(highest_changed, j);
			MIN1(lowest_changed, j-1);
		}
	}

	for (j = lowest_changed; j <= highest_changed; ++j)
		edge_array[j]->array_index = j;
}

__forceinline static U32 getEdgeCostVal(SEdge *e)
{
	U32 cost = *((U32*)&e->cost);
	cost = (cost & 0x7fffffff); // remove sign bit
	return cost | (e->uncollapsible ? 0x80000000 : 0);
}

__forceinline static U32 getBucketForCost(TriCutType *tc, U32 cost)
{
	U32 bucket;
	if (cost >= UNCOLLAPSIBLE_COST)
		return BUCKET_COUNT-1;
	if (cost <= tc->min_cost)
		return 0;
	bucket = (cost - tc->bucket_cost_offset) / tc->bucket_cost_divisor;
	return 1 + CLAMP(bucket, 0, BUCKET_COUNT-3);
}

void removeEdgeFromBucket(TriCutType *tc, SEdge *e)
{
#if SORT_PERIODICALLY || NO_SORTING
	ANALYSIS_ASSUME(e->bucket_index < BUCKET_COUNT);
	eaRemoveFast(&tc->edge_buckets[e->bucket_index].super_edges, e->array_index);
	eaiRemoveFast(&tc->edge_buckets[e->bucket_index].super_edge_costs, e->array_index);

	if ((int)e->array_index < eaSize(&tc->edge_buckets[e->bucket_index].super_edges))
	{
		ANALYSIS_ASSUME(tc->edge_buckets[e->bucket_index].super_edges);
		tc->edge_buckets[e->bucket_index].super_edges[e->array_index]->array_index = e->array_index;
	}
#else
	U32 i, count;

	ANALYSIS_ASSUME(e->bucket_index < BUCKET_COUNT);
	eaRemove(&tc->edge_buckets[e->bucket_index].super_edges, e->array_index);
	eaiRemove(&tc->edge_buckets[e->bucket_index].super_edge_costs, e->array_index);

	count = eaSize(&tc->edge_buckets[e->bucket_index].super_edges);
	for (i = e->array_index; i < count; ++i)
	{
		ANALYSIS_ASSUME(tc->edge_buckets[e->bucket_index].super_edges);
		tc->edge_buckets[e->bucket_index].super_edges[i]->array_index = i;
	}
#endif
}

static void updateEdgeCost(TriCutType *tc, SEdge *e, bool resort)
{
	U32 intcost = getEdgeCostVal(e);
	U32 new_bucket_index = getBucketForCost(tc, intcost);
	if (new_bucket_index != e->bucket_index)
	{
		U32 array_index2;

		// remove from old bucket
		removeEdgeFromBucket(tc, e);

		// add to new bucket
		assert(new_bucket_index < BUCKET_COUNT);
		e->bucket_index = new_bucket_index;
		e->array_index = eaPush(&tc->edge_buckets[e->bucket_index].super_edges, e);
		array_index2 = eaiPush(&tc->edge_buckets[e->bucket_index].super_edge_costs, intcost);
		DO_DEBUG_ASSERT(array_index2 == e->array_index);
	}
	else
	{
		ANALYSIS_ASSUME(e->bucket_index < BUCKET_COUNT);
		tc->edge_buckets[e->bucket_index].super_edge_costs[e->array_index] = intcost;
	}

#if !SORT_PERIODICALLY && !NO_SORTING
	if (resort && new_bucket_index != 0 && new_bucket_index != BUCKET_COUNT-1)
	{
		insertionSortSingleSEdge(tc->edge_buckets[new_bucket_index].super_edges, tc->edge_buckets[new_bucket_index].super_edge_costs, 
			eaSize(&tc->edge_buckets[new_bucket_index].super_edges), e->array_index);
	}
#endif
}

static int compareEdgesReverse(const SEdge **pe1, const SEdge **pe2)
{
	const SEdge *e1 = *pe1;
	const SEdge *e2 = *pe2;

	if (!e1->uncollapsible && e2->uncollapsible)
		return 1;

	if (e1->uncollapsible && !e2->uncollapsible)
		return -1;

	if (e1->cost > e2->cost)
		return -1;

	return e1->cost < e2->cost;
}

static bool invertsMesh(TriCutType *tc, int idx, Vec3 old_pos, Vec3 new_pos)
{
	Vec3 new_face_normal;
	F32 tri_area;
	int v0 = tc->mesh->tris[idx].idx[0];
	int v1 = tc->mesh->tris[idx].idx[1];
	int v2 = tc->mesh->tris[idx].idx[2];
	F32 *pos0 = tc->mesh->positions[v0];
	F32 *pos1 = tc->mesh->positions[v1];
	F32 *pos2 = tc->mesh->positions[v2];

	if (sameVec3(pos0, old_pos))
		pos0 = new_pos;
	if (sameVec3(pos1, old_pos))
		pos1 = new_pos;
	if (sameVec3(pos2, old_pos))
		pos2 = new_pos;
	if (isDegenerate(pos0, pos1, pos2))
		return true;
	if (tri_area = makePlaneNormal(pos0, pos1, pos2, new_face_normal))
	{
		if (tri_area > tc->triinfos[idx].area * 0.001f)
		{
			return	dotVec3(new_face_normal, tc->triinfos[idx].orig_face_normal) < 0.25f || 
					dotVec3(new_face_normal, tc->triinfos[idx].face_normal) < 0.25f || 
					(tc->is_terrain && new_face_normal[1] <= 0);
		}
	}

	// the triangle is degenerate, but is not one of the tris we were intending to remove
	// this reduction will probably cause a T junction, and eventually a mesh inversion
	return true;
}

// e1 is the error of collapsing towards vert1
// e2 is the error of collapsing towards vert2
static U32 setEdgeCost(TriCutType *tc, SEdge *e, bool update_parallel_cost)
{
	F32 e1=0, e2=0, e3=0;
	SVertInfo *svert1 = tc->svertinfos[e->svert1];
	SVertInfo *svert2 = tc->svertinfos[e->svert2];
	int i, use_best = 0, is_internal_edge = !svert1->on_texedge && !svert2->on_texedge && eaSize(&e->iedges) == 1;
	bool allowed1 = true, allowed2 = true, allowed3 = true, allowed = true;

	DO_DEBUG_ASSERT(eaSize(&e->iedges) > 0);

	e->uncollapsible = 0;
	e->connects_boundaries = 0;
	e->connects_creases = 0;
	e->is_texedge = eaSize(&e->iedges) > 1;
	e->connects_texedges = svert1->on_texedge && svert2->on_texedge && !e->is_texedge;

	// calculate error for collapsing this edge in either direction
	if (0)
	{
		IVec local_attributes = _alloca(tc->ivec_size);

		// CD: this doesn't work very well
		// this will compute the error based on all iedge iverts (all attributes)
		for (i = 0; i < eaSize(&e->iedges); i++)
		{
			F32 e1_1, e2_1;
			IVertInfo *ivert1 = &tc->ivertinfos[e->iedges[i]->ivert1];
			IVertInfo *ivert2 = &tc->ivertinfos[e->iedges[i]->ivert2];

			tc->subIVec(ivert1->attributes, ivert2->attributes, local_attributes); // get attributes relative to ivert2
			e1_1 = ivert1->local_error_value + tc->evaluateIQ(ivert2->local_error, local_attributes);

			tc->subIVec(ivert2->attributes, ivert1->attributes, local_attributes); // get attributes relative to ivert1
			e2_1 = ivert2->local_error_value + tc->evaluateIQ(ivert1->local_error, local_attributes);

			e1 += e1_1;
			e2 += e2_1;
		}
	}
	else if (is_internal_edge)
	{
		// for internal edges (not on a geometry or texture seam), use position and texcoord to calculate edge collapse error
		IVertInfo *ivert1 = &tc->ivertinfos[e->iedges[0]->ivert1];
		IVertInfo *ivert2 = &tc->ivertinfos[e->iedges[0]->ivert2];
		IVec local_attributes = _alloca(tc->ivec_size);

		tc->subIVec(ivert1->attributes, ivert2->attributes, local_attributes); // get attributes relative to ivert2
		e1 = ivert1->local_error_value + tc->evaluateIQ(ivert2->local_error, local_attributes);

		tc->subIVec(ivert2->attributes, ivert1->attributes, local_attributes); // get attributes relative to ivert1
		e2 = ivert2->local_error_value + tc->evaluateIQ(ivert1->local_error, local_attributes);
	}
	else
	{
		// for shared edges (connected to or along a texture seam), use only position to calculate edge collapse error
		SVec local_attributes;

		subSVec(svert1->attributes, svert2->attributes, local_attributes); // get attributes relative to svert2
		e1 = svert1->local_error_value + evaluateSQ(&svert2->local_error, local_attributes);

		subSVec(svert2->attributes, svert1->attributes, local_attributes); // get attributes relative to svert1
		e2 = svert2->local_error_value + evaluateSQ(&svert1->local_error, local_attributes);
	}

	// find affected tris
	eaiSetSize(&tc->faces_array1, 0);
	eaiSetSize(&tc->faces_array2, 0);
	for (i = 0; i < eaiSize(&svert1->faces); i++)
	{
		eaiPushUniqueInline(&tc->faces_array1, svert1->faces[i]);
	}
	for (i = 0; i < eaiSize(&svert2->faces); i++)
	{
		eaiPushUniqueInline(&tc->faces_array2, svert2->faces[i]);
	}
	for (i = 0; i < eaiSize(&e->faces); i++)
	{
		eaiFindAndRemoveFastInline(&tc->faces_array1, e->faces[i]);
		eaiFindAndRemoveFastInline(&tc->faces_array2, e->faces[i]);
	}

	for (i = 0; allowed1 && i < eaiSize(&tc->faces_array2); i++)
		allowed1 = !invertsMesh(tc, tc->faces_array2[i], svert2->v, svert1->v); // figure out if using svert1 causes a mesh inversion
	for (i = 0; allowed2 && i < eaiSize(&tc->faces_array1); i++)
		allowed2 = !invertsMesh(tc, tc->faces_array1[i], svert1->v, svert2->v); // figure out if using svert2 causes a mesh inversion

	// check if this is a geometry boundary edge (one edge and one face)
	e->is_boundary = edgeIsBoundary(tc, e);
	e->is_crease = edgeIsCrease(tc, e);

	// optimal placement
	if (tc->use_optimal_placement && is_internal_edge)
	{
		IVertInfo *ivert1 = &tc->ivertinfos[e->iedges[0]->ivert1];
		IVertInfo *ivert2 = &tc->ivertinfos[e->iedges[0]->ivert2];
		IQuadric iq = _alloca(tc->iquadric_size);
		tc->addIQ(ivert1->global_error, ivert2->global_error, iq);
		use_best = tc->optimizeIQ(iq, e->best);
		if (use_best)
		{
			IVec local_attributes1 = _alloca(tc->ivec_size), local_attributes2 = _alloca(tc->ivec_size);
			tc->subIVec(e->best, ivert1->attributes, local_attributes1); // get attributes relative to ivert1
			tc->subIVec(e->best, ivert2->attributes, local_attributes2); // get attributes relative to ivert2
			e3 = tc->evaluateIQ(ivert1->local_error, local_attributes1) + tc->evaluateIQ(ivert2->local_error, local_attributes2);

			e->bestv[0] = e->best[0] / tc->multiplier[0];
			e->bestv[1] = e->best[1] / tc->multiplier[1];
			e->bestv[2] = e->best[2] / tc->multiplier[2];
			addVec3(e->bestv, tc->bounds_min, e->bestv);

			for (i = 0; allowed3 && i < eaiSize(&tc->faces_array1); i++)
				allowed3 = !invertsMesh(tc, tc->faces_array1[i], svert1->v, e->bestv); // figure out if moving svert1 to the new position causes a mesh inversion
			for (i = 0; allowed3 && i < eaiSize(&tc->faces_array2); i++)
				allowed3 = !invertsMesh(tc, tc->faces_array2[i], svert2->v, e->bestv); // figure out if moving svert2 to the new position causes a mesh inversion
		}
	}

	// scale error by the size of the affected triangles
	MAX1(e1, MIN_ERROR);
	MAX1(e2, MIN_ERROR);
	MAX1(e3, MIN_ERROR);
	
	if (tc->scale_by_area)
	{
		e1 *= svert2->tri_area * tc->one_over_max_tri_area * tc->scale_by_area + 1 - tc->scale_by_area;
		e2 *= svert1->tri_area * tc->one_over_max_tri_area * tc->scale_by_area + 1 - tc->scale_by_area;
		e3 *= (svert1->tri_area + svert2->tri_area) * 0.5f * tc->one_over_max_tri_area * tc->scale_by_area + 1 - tc->scale_by_area;

		if (e->is_boundary || e->is_crease)
		{
			e1 *= 2 * tc->scale_by_area + 1 - tc->scale_by_area;
			e2 *= 2 * tc->scale_by_area + 1 - tc->scale_by_area;
		}
	}

	// maintain boundary conditions
	if (svert1->on_texedge && svert2->on_texedge)
	{
		use_best = 0;
		e1 += TEXEDGE_COST;
		e2 += TEXEDGE_COST;
	}
	else if (svert1->on_texedge)
	{
		use_best = 0;
		e1 += TOTEXEDGE_COST;
		e2 += e1 + TEXEDGE_COST; // make it expensive not to go to svert1
	}
	else if (svert2->on_texedge)
	{
		use_best = 0;
		e2 += TOTEXEDGE_COST;
		e1 += e2 + TEXEDGE_COST; // make it expensive not to go to svert2
	}

	if (svert1->on_boundary && svert2->on_boundary || e->is_boundary)
	{
		use_best = 0;
		if (!e->is_boundary)
			e->connects_boundaries = 1;
		if (tc->maintain_borders || e->connects_boundaries)
		{
			e1 += BOUNDARY_COST;
			e2 += BOUNDARY_COST;
			e3 += BOUNDARY_COST;
		}
	}
	else if (svert1->on_boundary)
	{
		use_best = 0;
		e2 += e1 + BOUNDARY_COST;
		allowed2 = false; // not allowed to collapse away from svert1
		allowed3 = false;
	}
	else if (svert2->on_boundary)
	{
		use_best = 0;
		e1 += e2 + BOUNDARY_COST;
		allowed1 = false; // not allowed to collapse away from svert2
		allowed3 = false;
	}

	if (svert1->on_crease && svert2->on_crease || e->is_crease)
	{
		use_best = 0;
		if (!e->is_crease)
			e->connects_creases = 1;
		if (tc->maintain_borders || e->connects_creases)
		{
			e1 += BOUNDARY_COST;
			e2 += BOUNDARY_COST;
			e3 += BOUNDARY_COST;
		}
	}
	else if (svert1->on_crease)
	{
		use_best = 0;
		e2 += e1 + BOUNDARY_COST;
		allowed2 = false; // not allowed to collapse away from svert1
		allowed3 = false;
	}
	else if (svert2->on_crease)
	{
		use_best = 0;
		e1 += e2 + BOUNDARY_COST;
		allowed1 = false; // not allowed to collapse away from svert2
		allowed3 = false;
	}

	if (svert1->is_locked && svert2->is_locked)
	{
		e1 += LOCKED_COST;
		e2 += LOCKED_COST;
		e3 += LOCKED_COST;
		allowed1 = false; // not allowed to collapse away from svert2
		allowed2 = false; // not allowed to collapse away from svert1
		allowed3 = false;
	}
	else if (svert1->is_locked)
	{
		use_best = 0;
		e2 += e1 + LOCKED_COST;
		allowed2 = false; // not allowed to collapse away from svert1
		allowed3 = false;
	}
	else if (svert2->is_locked)
	{
		use_best = 0;
		e1 += e2 + LOCKED_COST;
		allowed1 = false; // not allowed to collapse away from svert2
		allowed3 = false;
	}

	if (use_best && e3 < e1 && e3 < e2 && allowed3)
	{
		e->vert_to_use = USE_BEST;
		e->cost = e3;
	}
	else if (e1 <= e2 && allowed1)
	{
		e->vert_to_use = USE_VERT1;
		e->cost = e1;
	}
	else
	{
		e->vert_to_use = USE_VERT2;
		e->cost = e2;
		if (!allowed2)
			allowed = false;
	}

	if (tc->maintain_borders)
		e->uncollapsible = e->connects_creases || e->is_crease || e->connects_boundaries || e->is_boundary || e->connects_texedges || !allowed;
	else
		e->uncollapsible = e->connects_creases || e->connects_boundaries || e->connects_texedges || !allowed;


	LOG("setEdgeCost edge (%d,%d) to %.2g", e->svert1, e->svert2, e->cost);

	if (update_parallel_cost)
	{
		// update parallel edge cost array
		updateEdgeCost(tc, e, true);
	}

	return getEdgeCostVal(e);
}

//////////////////////////////////////////////////////////////////////////

__forceinline static bool sedgeMatch(SEdge *e, int svert1, int svert2, int *need_swap)
{
	if (e->svert1 == svert1 && e->svert2 == svert2)
	{
		*need_swap = 0;
		return true;
	}
	if (e->svert1 == svert2 && e->svert2 == svert1)
	{
		*need_swap = 1;
		return true;
	}

	return false;
}

static SEdge *getCreateSEdge(TriCutType *tc, int ivert1, int ivert2, int *need_swap)
{
	SVertInfo *svert1 = tc->ivertinfos[ivert1].svert;
	SVertInfo *svert2 = tc->ivertinfos[ivert2].svert;
	U32 array_index2, intcost;
	SEdge *e;
	int i;

	DO_DEBUG_ASSERT(!svert1->is_dead);
	DO_DEBUG_ASSERT(!svert2->is_dead);
	DO_DEBUG_ASSERT(svert1 != svert2);

	for (i = 0; i < eaSize(&svert1->sedges); i++)
	{
		e = svert1->sedges[i];
		if (sedgeMatch(e, svert1->svert, svert2->svert, need_swap))
			return e;
	}

	need_swap = 0;

	MP_CREATE_MEMBER(tc, SEdge, 500);
	e = MP_ALLOC_MEMBER(tc, SEdge);

	e->svert1 = svert1->svert;
	e->svert2 = svert2->svert;

	intcost = getEdgeCostVal(e);
	e->bucket_index = getBucketForCost(tc, intcost);
	e->array_index = eaPush(&tc->edge_buckets[e->bucket_index].super_edges, e);
	array_index2 = eaiPush(&tc->edge_buckets[e->bucket_index].super_edge_costs, intcost);
	DO_DEBUG_ASSERT(array_index2 == e->array_index);

	eaPushUnique(&svert1->sedges, e);
	eaPushUnique(&svert2->sedges, e);

	e->best = MP_ALLOC_MEMBER_NOPTRCAST(tc, IVec);

	return e;
}

static IEdge *getCreateIEdge(TriCutType *tc, int ivert1, int ivert2, int tex_id)
{
	int i, need_swap = 0;
	IEdge *e;

	DO_DEBUG_ASSERT(ivert1 != ivert2);
	DO_DEBUG_ASSERT(!tc->ivertinfos[ivert1].is_dead);
	DO_DEBUG_ASSERT(!tc->ivertinfos[ivert2].is_dead);

	for (i = 0; i < eaSize(&tc->ivertinfos[ivert1].iedges); i++)
	{
		e = tc->ivertinfos[ivert1].iedges[i];
		if (edgeMatch(e, ivert1, ivert2, tex_id))
		{
			eaPushUnique(&e->sedge->iedges, e);
			return e;
		}
	}

	MP_CREATE_MEMBER(tc, IEdge, 500);
	e = MP_ALLOC_MEMBER(tc, IEdge);

	e->ivert1 = ivert1;
	e->ivert2 = ivert2;
	e->tex_id = tex_id;

	eaPush(&tc->internal_edges, e);
	eaPushUnique(&tc->ivertinfos[ivert1].iedges, e);
	eaPushUnique(&tc->ivertinfos[ivert2].iedges, e);

	e->sedge = getCreateSEdge(tc, ivert1, ivert2, &need_swap);
	eaPushUnique(&e->sedge->iedges, e);

	if (need_swap)
	{
		int temp = e->ivert1;
		e->ivert1 = e->ivert2;
		e->ivert2 = temp;
	}

	LOG("createIEdge (%d,%d,T%d) sedge %d", e->ivert1, e->ivert2, e->tex_id, e->sedge->array_index);

	return e;
}

__forceinline static SVertInfo *getCreateSVert(TriCutType *tc, const Vec3 vert)
{
	SVertInfo *svi;
	IVec3 hash_pos;
	int *bucket;
	int i;

	hashVertex(vert, hash_pos);
	bucket = getVertexHashBucket(&tc->svert_hash, hash_pos);

	for (i = 0; i < eaiSize(&bucket); i++)
	{
		svi = tc->svertinfos[bucket[i]];
		if (nearSameVec3Tol(svi->v, vert, VECTOL))
			return svi;
	}

	MP_CREATE_MEMBER(tc, SVertInfo, 500);
	svi = MP_ALLOC_MEMBER(tc, SVertInfo);
	copyVec3(vert, svi->v);
	subSVec(vert, tc->bounds_min, svi->attributes);
	mulSVecSVec(svi->attributes, tc->multiplier, svi->attributes);
	svi->svert = eaPush(&tc->svertinfos, svi);

	addVertexToHashBucket(&tc->svert_hash, hash_pos, svi->svert);

	return svi;
}

static void combineSVerts(TriCutType *tc, SVertInfo *dead_svert, SVertInfo *new_svert, VertRemaps *vremaps)
{
	int j;

	DO_DEBUG_ASSERT(new_svert != dead_svert);

	dead_svert->is_dead = 1;

	LOG("killSVert %d", dead_svert->svert);

	DO_DEBUG_ASSERT(eaSize(&dead_svert->sedges) == 0);
	DO_DEBUG_ASSERT(eaiSize(&dead_svert->faces) == 0);

	for (j = 0; j < eaiSize(&dead_svert->iverts); j++)
	{
		int idx = dead_svert->iverts[j];
		IVertInfo *ivert = &tc->ivertinfos[idx];

		assert(idx >= 0 && idx < tc->mesh->vert_count);
		DO_DEBUG_ASSERT(!ivert->is_dead);
		DO_DEBUG_ASSERT(eaSize(&ivert->iedges) == 0);

		copyVec3(new_svert->v, tc->mesh->positions[idx]);
		copySVec(new_svert->attributes, ivert->attributes);

		ivert->svert = new_svert;
		eaiPushUniqueInline(&new_svert->iverts, idx);

		if (vremaps)
			addVertChange(	tc, vremaps, idx, 
							tc->mesh->positions[idx], 
							tc->mesh->tex1s?tc->mesh->tex1s[idx]:NULL, 
							tc->mesh->normals?tc->mesh->normals[idx]:NULL,
							tc->mesh->varcolors?&tc->mesh->varcolors[tc->mesh->varcolor_size*idx]:NULL);
	}

	new_svert->on_texedge = eaiSize(&new_svert->iverts) > 1;
	for (j = 0; !new_svert->on_texedge && j < eaSize(&new_svert->sedges); ++j)
		new_svert->on_texedge = eaSize(&new_svert->sedges[j]->iedges) > 1;

	new_svert->is_locked = 0;

	eaiDestroy(&dead_svert->iverts);
}

static void changeSVert(TriCutType *tc, SVertInfo *svert, IVec attributes, VertRemaps *vremaps)
{
	int i, j;

	DO_DEBUG_ASSERT(!svert->on_texedge);

	svert->v[0] = tc->bounds_min[0] + (attributes[0] / tc->multiplier[0]);
	svert->v[1] = tc->bounds_min[1] + (attributes[1] / tc->multiplier[1]);
	svert->v[2] = tc->bounds_min[2] + (attributes[2] / tc->multiplier[2]);

	copySVec(attributes, svert->attributes);

	for (j = 0; j < eaiSize(&svert->iverts); j++)
	{
		int idx = svert->iverts[j];
		F32 *v = tc->ivertinfos[idx].attributes;

		assert(idx >= 0 && idx < tc->mesh->vert_count);

		copyVec3(svert->v, tc->mesh->positions[idx]);
		copyVec3(tc->mesh->positions[idx], v);
		i = 3;

		if (tc->mesh->usagebits & USE_TEX1S)
		{
			tc->mesh->tex1s[idx][0] = tc->bounds_min[i+0] + (attributes[i+0] / tc->multiplier[i+0]);
			tc->mesh->tex1s[idx][1] = tc->bounds_min[i+1] + (attributes[i+1] / tc->multiplier[i+1]);
			copyVec2(tc->mesh->tex1s[idx], &v[i]);
			i += 2;
		}

		if (tc->mesh->usagebits & USE_NORMALS)
		{
			tc->mesh->normals[idx][0] = tc->bounds_min[i+0] + (attributes[i+0] / tc->multiplier[i+0]);
			tc->mesh->normals[idx][1] = tc->bounds_min[i+1] + (attributes[i+1] / tc->multiplier[i+1]);
			tc->mesh->normals[idx][2] = tc->bounds_min[i+2] + (attributes[i+2] / tc->multiplier[i+2]);
			normalVec3(tc->mesh->normals[idx]);
			copyVec3(tc->mesh->normals[idx], &v[i]);
			i += 3;
		}

		if (tc->mesh->usagebits & USE_COLORS)
		{
			int k;
			for (k = 0; k < 4; ++k)
			{
				int val = round(tc->bounds_min[i+k] + (attributes[i+k] / tc->multiplier[i+k]));
				tc->mesh->colors[idx].rgba[k] = CLAMP(val, 0, 255);
			}
			if (tc->is_terrain)
				ColorNormalize(&tc->mesh->colors[idx]);
			copyVec4(tc->mesh->colors[idx].rgba, &v[i]);
			i += 4;
		}

		if (tc->mesh->usagebits & USE_VARCOLORS)
		{
			int k;
			for (k = 0; k < tc->mesh->varcolor_size; ++k)
			{
				int val = round(tc->bounds_min[i+k] + (attributes[i+k] / tc->multiplier[i+k]));
				tc->mesh->varcolors[idx * tc->mesh->varcolor_size + k] = CLAMP(val, 0, 255);
			}
			if (tc->is_terrain)
				VarColorNormalize(&tc->mesh->varcolors[idx * tc->mesh->varcolor_size], tc->mesh->varcolor_size);
			for (k = 0; k < tc->mesh->varcolor_size; ++k)
				v[i + k] = tc->mesh->varcolors[idx * tc->mesh->varcolor_size + k];
			i += tc->mesh->varcolor_size;
		}

		tc->subIVec(v, tc->bounds_min, v);
		tc->mulIVecIVec(v, tc->multiplier, v);

		addVertChange(	tc, vremaps, idx, 
						tc->mesh->positions[idx], 
						tc->mesh->tex1s?tc->mesh->tex1s[idx]:NULL, 
						tc->mesh->normals?tc->mesh->normals[idx]:NULL,
						tc->mesh->varcolors?&tc->mesh->varcolors[tc->mesh->varcolor_size*idx]:NULL);
	}
}

//////////////////////////////////////////////////////////////////////////

static void updateAllBuckets(TriCutType *tc)
{
	U32 highest_cost_val = 1;
	int i, j, count;
	F32 cost, last_one_over_max_tri_area = tc->one_over_max_tri_area, multiplier;

	tc->one_over_max_tri_area = 0;
	tc->min_tri_area = 8e16;

	for (i = 0; i < eaSize(&tc->svertinfos); i++)
	{
		MIN1(tc->min_tri_area, tc->ivertinfos[i].svert->tri_area);
		MAX1(tc->one_over_max_tri_area, tc->ivertinfos[i].svert->tri_area);
	}

	tc->one_over_max_tri_area = tc->one_over_max_tri_area ? (1.f / tc->one_over_max_tri_area) : 1;
	tc->one_over_max_tri_area = 1; // TODO(CD) do I still want this here?
	cost = MIN_ERROR * tc->min_tri_area * tc->one_over_max_tri_area;
	tc->min_cost = ((*((U32 *)&cost)) & 0x7fffffff);

	LOG("updateAllBuckets minCost -> %d, minTriArea -> %f, oneOverMaxTriArea -> %.2g", tc->min_cost, tc->min_tri_area, tc->one_over_max_tri_area);

	multiplier = tc->one_over_max_tri_area / last_one_over_max_tri_area;

	tc->bucket_cost_offset = UNCOLLAPSIBLE_COST;

	// sort edges, then recompute bucket cost divisor by finding the highest cost
	for (j = 0; j < BUCKET_COUNT; ++j)
	{
		count = eaSize(&tc->edge_buckets[j].super_edges);
		if (count)
		{
			for (i = 0; i < count; ++i)
				tc->edge_buckets[j].super_edges[i]->cost *= multiplier;

			// sort edges in reverse because we will assign them to buckets in reverse
			qsortG(tc->edge_buckets[j].super_edges, count, sizeof(*tc->edge_buckets[j].super_edges), compareEdgesReverse);

			for (i = 0; i < count; ++i)
			{
				U32 highest_bucket_cost_val = getEdgeCostVal(tc->edge_buckets[j].super_edges[i]);
				if (highest_bucket_cost_val < UNCOLLAPSIBLE_COST)
				{
					MAX1(highest_cost_val, highest_bucket_cost_val);
					break;
				}
			}

			for (i = count-1; i >= 0; --i)
			{
				U32 lowest_bucket_cost_val = getEdgeCostVal(tc->edge_buckets[j].super_edges[i]);
				if (lowest_bucket_cost_val > 0)
				{
					MIN1(tc->bucket_cost_offset, lowest_bucket_cost_val);
					break;
				}
				if (lowest_bucket_cost_val >= tc->bucket_cost_offset)
					break;
			}
		}
	}

	if (tc->bucket_cost_offset == UNCOLLAPSIBLE_COST || highest_cost_val < tc->bucket_cost_offset)
		tc->bucket_cost_offset = 0;

	tc->bucket_cost_divisor = (highest_cost_val - tc->bucket_cost_offset) / (BUCKET_COUNT-2);
	MAX1(tc->bucket_cost_divisor, 1);

	for (j = 0; j < BUCKET_COUNT; ++j)
	{
		count = eaSize(&tc->edge_buckets[j].super_edges);

		DO_DEBUG_ASSERT(count == eaiSize(&tc->edge_buckets[j].super_edge_costs));

		// update bucket, parallel cost array, and array indices
		// this also puts the edges back into sorted order
		for (i = count-1; i >= 0; --i)
		{
			ANALYSIS_ASSUME(tc->edge_buckets[j].super_edge_costs && tc->edge_buckets[j].super_edges);
			tc->edge_buckets[j].super_edges[i]->array_index = i;
			updateEdgeCost(tc, tc->edge_buckets[j].super_edges[i], false);
		}
	}

	tc->force_resort = true;
}

static void computeQuadrics(TriCutType *tc, U32 old_cost_val, U32 old_bucket)
{
	IQuadric iq = _alloca(tc->iquadric_size), iq_global = _alloca(tc->iquadric_size);
	IVec iv0 = _alloca(tc->ivec_size), iv1 = _alloca(tc->ivec_size), iv2 = _alloca(tc->ivec_size);
	int i, j, count, count2;

	eaiSetSize(&tc->changed_faces, 0);
	eaSetSize(&tc->changed_edges, 0);

	// set all vertex quadrics to zero
	count = eaiSize(&tc->changed_iverts);
	for (i = 0; i < count; i++)
	{
		IVertInfo *ivert = &tc->ivertinfos[tc->changed_iverts[i]];
		SVertInfo *svert = ivert->svert;
		zeroSQ(&svert->local_error);
		memset(ivert->local_error, 0, tc->iquadric_size);
		memset(ivert->global_error, 0, tc->iquadric_size);
		svert->tri_area = 0;

		count2 = eaiSize(&svert->faces);
		for (j = 0; j < count2; j++)
			eaiPushUniqueInline(&tc->changed_faces, svert->faces[j]);
	}

	// add faces to vertex quadrics
	count = eaiSize(&tc->changed_faces);
	for (i = 0; i < count; i++)
	{
		int face = tc->changed_faces[i];
		int v0 = tc->mesh->tris[face].idx[0], v1 = tc->mesh->tris[face].idx[1], v2 = tc->mesh->tris[face].idx[2];
		SVec sv0, sv1, sv2;
		F32 area = 1;
		SQuadric sq;

		if (tc->triinfos[face].is_dead)
			continue;

		if (tc->scale_by_area)
		{
			area = gmeshGetTriArea(tc->mesh, face);
			assert(FINITE(area));
		}

		tc->initFromTriIQ(iq_global, tc->ivertinfos[v0].attributes, tc->ivertinfos[v1].attributes, tc->ivertinfos[v2].attributes);

		if (eaiFind(&tc->changed_iverts, v0) >= 0)
		{
			tc->subIVec(tc->ivertinfos[v0].attributes, tc->ivertinfos[v0].attributes, iv0);
			tc->subIVec(tc->ivertinfos[v1].attributes, tc->ivertinfos[v0].attributes, iv1);
			tc->subIVec(tc->ivertinfos[v2].attributes, tc->ivertinfos[v0].attributes, iv2);
			tc->initFromTriIQ(iq, iv0, iv1, iv2);
			tc->addToIQ(iq, tc->ivertinfos[v0].local_error);

			tc->addToIQ(iq_global, tc->ivertinfos[v0].global_error);

			subSVec(tc->ivertinfos[v0].svert->attributes, tc->ivertinfos[v0].svert->attributes, sv0);
			subSVec(tc->ivertinfos[v1].svert->attributes, tc->ivertinfos[v0].svert->attributes, sv1);
			subSVec(tc->ivertinfos[v2].svert->attributes, tc->ivertinfos[v0].svert->attributes, sv2);
			initFromTriSQ(&sq, sv0, sv1, sv2);
			addToSQ(&sq, &tc->ivertinfos[v0].svert->local_error);

			tc->ivertinfos[v0].svert->tri_area += area;
		}

		if (eaiFind(&tc->changed_iverts, v1) >= 0)
		{
			tc->subIVec(tc->ivertinfos[v0].attributes, tc->ivertinfos[v1].attributes, iv0);
			tc->subIVec(tc->ivertinfos[v1].attributes, tc->ivertinfos[v1].attributes, iv1);
			tc->subIVec(tc->ivertinfos[v2].attributes, tc->ivertinfos[v1].attributes, iv2);
			tc->initFromTriIQ(iq, iv1, iv2, iv0);
			tc->addToIQ(iq, tc->ivertinfos[v1].local_error);

			tc->addToIQ(iq_global, tc->ivertinfos[v1].global_error);

			subSVec(tc->ivertinfos[v0].svert->attributes, tc->ivertinfos[v1].svert->attributes, sv0);
			subSVec(tc->ivertinfos[v1].svert->attributes, tc->ivertinfos[v1].svert->attributes, sv1);
			subSVec(tc->ivertinfos[v2].svert->attributes, tc->ivertinfos[v1].svert->attributes, sv2);
			initFromTriSQ(&sq, sv1, sv2, sv0);
			addToSQ(&sq, &tc->ivertinfos[v1].svert->local_error);

			tc->ivertinfos[v1].svert->tri_area += area;
		}

		if (eaiFind(&tc->changed_iverts, v2) >= 0)
		{
			tc->subIVec(tc->ivertinfos[v0].attributes, tc->ivertinfos[v2].attributes, iv0);
			tc->subIVec(tc->ivertinfos[v1].attributes, tc->ivertinfos[v2].attributes, iv1);
			tc->subIVec(tc->ivertinfos[v2].attributes, tc->ivertinfos[v2].attributes, iv2);
			tc->initFromTriIQ(iq, iv2, iv0, iv1);
			tc->addToIQ(iq, tc->ivertinfos[v2].local_error);

			tc->addToIQ(iq_global, tc->ivertinfos[v2].global_error);

			subSVec(tc->ivertinfos[v0].svert->attributes, tc->ivertinfos[v2].svert->attributes, sv0);
			subSVec(tc->ivertinfos[v1].svert->attributes, tc->ivertinfos[v2].svert->attributes, sv1);
			subSVec(tc->ivertinfos[v2].svert->attributes, tc->ivertinfos[v2].svert->attributes, sv2);
			initFromTriSQ(&sq, sv2, sv0, sv1);
			addToSQ(&sq, &tc->ivertinfos[v2].svert->local_error);

			tc->ivertinfos[v2].svert->tri_area += area;
		}
	}

	// calculate edge contraction cost and vertex placement
	count = eaiSize(&tc->changed_iverts);
	for (i = 0; i < count; i++)
	{
		IVertInfo *ivert = &tc->ivertinfos[tc->changed_iverts[i]];
		SVertInfo *svert = ivert->svert;

		ivert->local_error_value = tc->evaluateIQ(ivert->local_error, tc->zeroivec);
		svert->local_error_value = evaluateSQ(&svert->local_error, zerosvec);

		count2 = eaSize(&svert->sedges);
		for (j = 0; j < count2; j++)
			eaPushUnique(&tc->changed_edges, svert->sedges[j]);

		svert->on_texedge = eaiSize(&svert->iverts) > 1;
		for (j = 0; !svert->on_texedge && j < count2; ++j)
			svert->on_texedge = eaSize(&svert->sedges[j]->iedges) > 1;
	}

	count = eaSize(&tc->changed_edges);
	for (i = 0; i < count; i++)
	{
		U32 changed_edge_cost_val = setEdgeCost(tc, tc->changed_edges[i], true);
		MIN1(old_cost_val, changed_edge_cost_val);
	}

#if SORT_PERIODICALLY && !NO_SORTING
	// bucket BUCKET_COUNT-1 has all uncollapsible edges, no need to sort it
	for (i = 0; i < BUCKET_COUNT-1; ++i)
	{
		// sort edges
		count = eaSize(&tc->edge_buckets[i].super_edges);
		DO_DEBUG_ASSERT(count == eaiSize(&tc->edge_buckets[i].super_edge_costs));

		if (count)
		{
			if (i > 0 && (					// bucket 0 has all cost 0 edges, no need to sort it at all
				i != (int)old_bucket ||		// always sort if this is a new bucket
				tc->force_resort ||
				tc->edge_buckets[i].super_edge_costs[0] > old_cost_val // otherwise only sort if the cost of the next edge in the bucket is very different from the last collapsed edge
				))
			{
				LOG("insertionSort bucket %d, %d edges", i, count);
				insertionSortSEdges(tc->edge_buckets[i].super_edges, tc->edge_buckets[i].super_edge_costs, count);
			}
			tc->force_resort = false;
			break;
		}
	}
#endif
}

void computeAllQuadrics(TriCutType *tc)
{
	IQuadric iq = _alloca(tc->iquadric_size), iq_global = _alloca(tc->iquadric_size);
	IVec iv0 = _alloca(tc->ivec_size), iv1 = _alloca(tc->ivec_size), iv2 = _alloca(tc->ivec_size);
	int i, j, count;

	// set all vertex quadrics to zero
	for (i = 0; i < eaSize(&tc->svertinfos); i++)
	{
		zeroSQ(&tc->svertinfos[i]->local_error);
		tc->ivertinfos[i].svert->tri_area = 0;
	}

	for (i = 0; i < tc->mesh->vert_count; i++)
	{
		memset(tc->ivertinfos[i].local_error, 0, tc->iquadric_size);
		memset(tc->ivertinfos[i].global_error, 0, tc->iquadric_size);
	}

	// add faces to vertex quadrics
	for (i = 0; i < tc->mesh->tri_count; i++)
	{
		int v0 = tc->mesh->tris[i].idx[0], v1 = tc->mesh->tris[i].idx[1], v2 = tc->mesh->tris[i].idx[2];
		SVec sv0, sv1, sv2;
		F32 area = 1;
		SQuadric sq;

		if (tc->triinfos[i].is_dead)
			continue;

		if (tc->scale_by_area)
		{
			area = gmeshGetTriArea(tc->mesh, i);
			assert(FINITE(area));
		}

		tc->initFromTriIQ(iq_global, tc->ivertinfos[v0].attributes, tc->ivertinfos[v1].attributes, tc->ivertinfos[v2].attributes);

		{
			tc->subIVec(tc->ivertinfos[v0].attributes, tc->ivertinfos[v0].attributes, iv0);
			tc->subIVec(tc->ivertinfos[v1].attributes, tc->ivertinfos[v0].attributes, iv1);
			tc->subIVec(tc->ivertinfos[v2].attributes, tc->ivertinfos[v0].attributes, iv2);
			tc->initFromTriIQ(iq, iv0, iv1, iv2);
			tc->addToIQ(iq, tc->ivertinfos[v0].local_error);

			tc->addToIQ(iq_global, tc->ivertinfos[v0].global_error);

			subSVec(tc->ivertinfos[v0].svert->attributes, tc->ivertinfos[v0].svert->attributes, sv0);
			subSVec(tc->ivertinfos[v1].svert->attributes, tc->ivertinfos[v0].svert->attributes, sv1);
			subSVec(tc->ivertinfos[v2].svert->attributes, tc->ivertinfos[v0].svert->attributes, sv2);
			initFromTriSQ(&sq, sv0, sv1, sv2);
			addToSQ(&sq, &tc->ivertinfos[v0].svert->local_error);

			tc->ivertinfos[v0].svert->tri_area += area;
		}

		{
			tc->subIVec(tc->ivertinfos[v0].attributes, tc->ivertinfos[v1].attributes, iv0);
			tc->subIVec(tc->ivertinfos[v1].attributes, tc->ivertinfos[v1].attributes, iv1);
			tc->subIVec(tc->ivertinfos[v2].attributes, tc->ivertinfos[v1].attributes, iv2);
			tc->initFromTriIQ(iq, iv1, iv2, iv0);
			tc->addToIQ(iq, tc->ivertinfos[v1].local_error);

			tc->addToIQ(iq_global, tc->ivertinfos[v1].global_error);

			subSVec(tc->ivertinfos[v0].svert->attributes, tc->ivertinfos[v1].svert->attributes, sv0);
			subSVec(tc->ivertinfos[v1].svert->attributes, tc->ivertinfos[v1].svert->attributes, sv1);
			subSVec(tc->ivertinfos[v2].svert->attributes, tc->ivertinfos[v1].svert->attributes, sv2);
			initFromTriSQ(&sq, sv1, sv2, sv0);
			addToSQ(&sq, &tc->ivertinfos[v1].svert->local_error);

			tc->ivertinfos[v1].svert->tri_area += area;
		}

		{
			tc->subIVec(tc->ivertinfos[v0].attributes, tc->ivertinfos[v2].attributes, iv0);
			tc->subIVec(tc->ivertinfos[v1].attributes, tc->ivertinfos[v2].attributes, iv1);
			tc->subIVec(tc->ivertinfos[v2].attributes, tc->ivertinfos[v2].attributes, iv2);
			tc->initFromTriIQ(iq, iv2, iv0, iv1);
			tc->addToIQ(iq, tc->ivertinfos[v2].local_error);

			tc->addToIQ(iq_global, tc->ivertinfos[v2].global_error);

			subSVec(tc->ivertinfos[v0].svert->attributes, tc->ivertinfos[v2].svert->attributes, sv0);
			subSVec(tc->ivertinfos[v1].svert->attributes, tc->ivertinfos[v2].svert->attributes, sv1);
			subSVec(tc->ivertinfos[v2].svert->attributes, tc->ivertinfos[v2].svert->attributes, sv2);
			initFromTriSQ(&sq, sv2, sv0, sv1);
			addToSQ(&sq, &tc->ivertinfos[v2].svert->local_error);

			tc->ivertinfos[v2].svert->tri_area += area;
		}
	}

	for (i = 0; i < eaSize(&tc->svertinfos); i++)
		tc->svertinfos[i]->local_error_value = evaluateSQ(&tc->svertinfos[i]->local_error, zerosvec);

	for (i = 0; i < tc->mesh->vert_count; i++)
		tc->ivertinfos[i].local_error_value = tc->evaluateIQ(tc->ivertinfos[i].local_error, tc->zeroivec);

	for (j = 0; j < BUCKET_COUNT; ++j)
	{
		// calculate edge contraction cost and vertex placement
		count = eaSize(&tc->edge_buckets[j].super_edges);
		for (i = 0; i < count; ++i)
			setEdgeCost(tc, tc->edge_buckets[j].super_edges[i], false);
	}

	updateAllBuckets(tc);
}

//////////////////////////////////////////////////////////////////////////

// remove references to old edge, free old edge
static void removeEdge(TriCutType *tc, SEdge *old)
{
	int i, count;
	IEdge *e_old;

	count = eaiSize(&old->faces);
	for (i = 0; i < count; i++)
	{
		// remove the dead edge on its tris
		int j;
		for (j = 0; j < 3; j++)
		{
			if (tc->triinfos[old->faces[i]].sedges[j] == old)
				tc->triinfos[old->faces[i]].sedges[j] = 0;
		}
	}
	eaiSetSize(&old->faces, 0);

	while (e_old = eaPop(&old->iedges))
	{
		// remove old edge from its verts
		eaFindAndRemoveFastInline(&tc->ivertinfos[e_old->ivert1].iedges, e_old);
		eaFindAndRemoveFastInline(&tc->ivertinfos[e_old->ivert2].iedges, e_old);

		// free old edge
		eaFindAndRemoveFastInline(&tc->internal_edges, e_old);
		freeIEdge(tc, e_old);
	}

	// remove old edge from its verts
	eaFindAndRemoveFastInline(&tc->svertinfos[old->svert1]->sedges, old);
	eaFindAndRemoveFastInline(&tc->svertinfos[old->svert2]->sedges, old);

	// free old edge
	removeEdgeFromBucket(tc, old);

	DO_DEBUG_ASSERT(eaSize(&old->iedges) == 0);
	freeSEdge(tc, old);
}

void calculateTriInfo(TriCutType *tc, int idx, int init)
{
	int v0 = tc->mesh->tris[idx].idx[0];
	int v1 = tc->mesh->tris[idx].idx[1];
	int v2 = tc->mesh->tris[idx].idx[2];
	int tex = tc->mesh->tris[idx].tex_id;
	IEdge *e0, *e1, *e2;

	if (!tc->triinfos[idx].is_dead && isTriDegenerate(tc, &tc->mesh->tris[idx]))
	{
//		printf("error in calculateTriInfo (mesh reduction), may cause holes in geometry\n");
		killTri(tc, idx);
		return;
	}

	if (init)
		makePlaneNormal(tc->mesh->positions[v0], tc->mesh->positions[v1], tc->mesh->positions[v2], tc->triinfos[idx].orig_face_normal);

	tc->triinfos[idx].area = makePlaneNormal(tc->mesh->positions[v0], tc->mesh->positions[v1], tc->mesh->positions[v2], tc->triinfos[idx].face_normal);


	// set tri edges
	e0 = getCreateIEdge(tc, v0, v1, tex);
	e1 = getCreateIEdge(tc, v1, v2, tex);
	e2 = getCreateIEdge(tc, v2, v0, tex);
	tc->triinfos[idx].sedges[0] = e0->sedge;
	tc->triinfos[idx].sedges[1] = e1->sedge;
	tc->triinfos[idx].sedges[2] = e2->sedge;

	// add tri to the face lists of its edges
	eaiPushUniqueInline(&e0->sedge->faces, idx);
	eaiPushUniqueInline(&e1->sedge->faces, idx);
	eaiPushUniqueInline(&e2->sedge->faces, idx);

	// add tri to the face lists of its verts
	eaiPushUniqueInline(&tc->ivertinfos[v0].svert->faces, idx);
	eaiPushUniqueInline(&tc->ivertinfos[v1].svert->faces, idx);
	eaiPushUniqueInline(&tc->ivertinfos[v2].svert->faces, idx);

	LOG("calculateTriInfo idx %d, v0 %d, v1 %d, v2 %d, tex %d", idx, v0, v1, v2, tex);
}

void setVertAttributes(TriCutType *tc, int idx)
{
	F32 *v = tc->ivertinfos[idx].attributes;
	int i;

	assert(tc->mesh->positions);

	copyVec3(tc->mesh->positions[idx], v);
	i = 3;

	if (tc->mesh->usagebits & USE_TEX1S)
	{
		copyVec2(tc->mesh->tex1s[idx], &v[i]);
		i += 2;
	}

	if (tc->mesh->usagebits & USE_NORMALS)
	{
		copyVec3(tc->mesh->normals[idx], &v[i]);
		i += 3;
	}

	if (tc->mesh->usagebits & USE_COLORS)
	{
		copyVec4(tc->mesh->colors[idx].rgba, &v[i]);
		i += 4;
	}

	if (tc->mesh->usagebits & USE_VARCOLORS)
	{
		int j;
		for (j = 0; j < tc->mesh->varcolor_size; ++j)
			v[i + j] = tc->mesh->varcolors[idx * tc->mesh->varcolor_size + j];
		i += tc->mesh->varcolor_size;
	}

	tc->subIVec(v, tc->bounds_min, v);
	tc->mulIVecIVec(v, tc->multiplier, v);

	tc->ivertinfos[idx].svert = getCreateSVert(tc, tc->mesh->positions[idx]);
	eaiPushUniqueInline(&tc->ivertinfos[idx].svert->iverts, idx);

	LOG("setVertAttributes idx %d, svert %d, attributes (%.2g.,%.2g,%.2g)", idx, tc->ivertinfos[idx].svert->svert, v[0], v[1], v[2]);
}

static void collapsingGetTriOtherTwoVerts(TriCutType *tc, TriInfo *tri, SVertInfo *svert, SVertInfo **first, SVertInfo **second)
{
	int i;
	(*first) = NULL;
	(*second) = NULL;
	for ( i=0; i < 3; i++ ) {
		SVertInfo *svert_new = tc->svertinfos[tri->sedges[i]->svert1];
		if(	!nearSameVec3Tol(svert->v, svert_new->v, VECTOL) &&
			(!(*first) || !nearSameVec3Tol((*first)->v, svert_new->v, VECTOL)) &&
			(!(*second) || !nearSameVec3Tol((*second)->v, svert_new->v, VECTOL)) ){
			if(!(*first))
				(*first) = svert_new;
			else if(!(*second)) {
				(*second) = svert_new;
				return;
			}
		}
		svert_new = tc->svertinfos[tri->sedges[i]->svert2];
		if(	!nearSameVec3Tol(svert->v, svert_new->v, VECTOL) &&
			(!(*first) || !nearSameVec3Tol((*first)->v, svert_new->v, VECTOL)) &&
			(!(*second) || !nearSameVec3Tol((*second)->v, svert_new->v, VECTOL)) ){
				if(!(*first))
					(*first) = svert_new;
				else if(!(*second)) {
					(*second) = svert_new;
					return;
				}
		}
	}
}

static void collapsingGetTriNormal(Vec3 p1, Vec3 p2, Vec3 p3, Vec3 n)
{
	Vec3 a, b;
	subVec3(p2, p1, a);
	subVec3(p3, p1, b);
	crossVec3(a, b, n);
	normalVec3(n);
	if(n[1] < 0)
		scaleVec3(n, -1.0f, n);
}

static bool collapsingToVertWillDamage(TriCutType *tc, SVertInfo *svert, SVertInfo *moving_to_svert)
{
	int i;
	for ( i=0; i < eaiSize(&svert->faces); i++ ) {
		TriInfo *current_tri = &tc->triinfos[svert->faces[i]];
		SVertInfo *other_svert_1=NULL;
		SVertInfo *other_svert_2=NULL;
		Vec3 new_normal;
		Vec3 diff_vec;
		F32 virt_diff;

		collapsingGetTriOtherTwoVerts(tc, current_tri, svert, &other_svert_1, &other_svert_2);

		if (nearSameVec3Tol(moving_to_svert->v, other_svert_1->v, VECTOL))
			continue;
		if (nearSameVec3Tol(moving_to_svert->v, other_svert_2->v, VECTOL))
			continue;

		collapsingGetTriNormal(moving_to_svert->v, other_svert_1->v, other_svert_2->v, new_normal);
		subVec3(current_tri->orig_face_normal, new_normal, diff_vec);
		virt_diff = ABS(diff_vec[0]) + ABS(diff_vec[1]) + ABS(diff_vec[2]);
		if (virt_diff > 0.75f && !sameVec3(zerovec3, new_normal)) {
			return true;
		}
	}
	return false;
}

static bool collapsingEdgeWillDamage(TriCutType *tc, SEdge *sedge)
{
	SVertInfo *svert1 = tc->svertinfos[sedge->svert1];
	SVertInfo *svert2 = tc->svertinfos[sedge->svert2];
	if(collapsingToVertWillDamage(tc, svert1, svert2))
		return true;
	if(collapsingToVertWillDamage(tc, svert2, svert1))
		return true;
	return false;
}

// this is the complicated part...
ReduceInstruction *collapseEdge(TriCutType *tc, SEdge *sedge)
{
	int i, j, dead_count;
	ReduceInstruction *ri;
	VertUse last_use = sedge->vert_to_use;
	Vec3 last_new = {0};
	int new_sidx, dead_sidx;
	F32 *last_attributes1 = NULL, *last_attributes2 = NULL;
	SVertInfo *dead_svi;
	SVertInfo *new_svi;
	U32 old_cost_val = getEdgeCostVal(sedge);
	U32 old_bucket = sedge->bucket_index;
	VertUse vert_to_use = sedge->vert_to_use;
	F32 *best = NULL;

	// Based on terrainGetDesiredMeshTriCount target error
	if(tc->is_terrain && tc->target_error < 0.5f && !gConf.bPreventTerrainMeshImprovements) {
		if(collapsingEdgeWillDamage(tc, sedge)) {
			removeEdgeFromBucket(tc, sedge);
			return NULL;
		}
	}

	eaSetSize(&tc->changed_edges, 0);
	eaiSetSize(&tc->changed_faces, 0);
	eaiSetSize(&tc->changed_iverts, 0);
	eaiSetSize(&tc->dead_idxs, 0);
	eaiSetSize(&tc->new_idxs, 0);

	if (vert_to_use == USE_BEST)
	{
		new_sidx = sedge->svert1;
		dead_sidx = sedge->svert2;
		copyVec3(sedge->bestv, last_new);
		best = _alloca(tc->ivec_size);
		memcpy(best, sedge->best, tc->ivec_size);
		LOG("collapseEdge s%d -> s%d, best (%.2g, %.2g, %.2g), error %.2g", 
			dead_sidx, 
			new_sidx, 
			sedge->bestv[0], sedge->bestv[1], sedge->bestv[2], 
			sedge->cost);
	}
	else if (vert_to_use == USE_VERT1)
	{
		new_sidx = sedge->svert1;
		dead_sidx = sedge->svert2;
		copyVec3(tc->svertinfos[new_sidx]->v, last_new);
		LOG("collapseEdge s%d -> s%d, error %.2g", 
			dead_sidx, 
			new_sidx, 
			sedge->cost);
	}
	else
	{
		new_sidx = sedge->svert2;
		dead_sidx = sedge->svert1;
		copyVec3(tc->svertinfos[new_sidx]->v, last_new);
		LOG("collapseEdge s%d -> s%d, error %.2g", 
			dead_sidx, 
			new_sidx, 
			sedge->cost);
	}

	dead_svi = tc->svertinfos[dead_sidx];
	new_svi = tc->svertinfos[new_sidx];

	if (vert_to_use == USE_BEST)
	{
		// this is just used for debugging purposes
		
		last_attributes1 = _alloca(tc->ivec_size);
		memcpy(last_attributes1, tc->ivertinfos[dead_svi->iverts[0]].attributes, tc->ivec_size);

		last_attributes2 = _alloca(tc->ivec_size);
		memcpy(last_attributes2, tc->ivertinfos[new_svi->iverts[0]].attributes, tc->ivec_size);
	}

	MP_CREATE_MEMBER(tc, ReduceInstruction, 500);
	ri = MP_ALLOC_MEMBER(tc, ReduceInstruction);

	ri->cost = sedge->cost;

	if (vert_to_use != USE_BEST && tc->mesh->tex1s)
	{
		// find orphaned iverts on the dead svert
		eaiCopy(&tc->dead_idxs, &dead_svi->iverts);
		for (j = 0; j < eaSize(&sedge->iedges); ++j)
		{
			if (vert_to_use == USE_VERT1)
				eaiFindAndRemoveFastInline(&tc->dead_idxs, sedge->iedges[j]->ivert2);
			else
				eaiFindAndRemoveFastInline(&tc->dead_idxs, sedge->iedges[j]->ivert1);
		}

		// try to find an iedge to match each orphaned ivert based on texcoords
		for (i = 0; i < eaiSize(&tc->dead_idxs); ++i)
		{
			for (j = 0; j < eaSize(&sedge->iedges); ++j)
			{
				IEdge *e = sedge->iedges[j];
				if (vert_to_use == USE_VERT1)
				{
					if (nearSameVec2(tc->mesh->tex1s[e->ivert2], tc->mesh->tex1s[tc->dead_idxs[i]]))
					{
						// copy texcoord attributes from other end of the edge
						copyVec2(&tc->ivertinfos[e->ivert1].attributes[3], &tc->ivertinfos[tc->dead_idxs[i]].attributes[3]);
						copyVec2(tc->mesh->tex1s[e->ivert1], tc->mesh->tex1s[tc->dead_idxs[i]]);
						if (tc->mesh->normals)
							copyVec3(tc->mesh->normals[e->ivert1], tc->mesh->normals[tc->dead_idxs[i]]);
						if (tc->mesh->binormals)
							copyVec3(tc->mesh->binormals[e->ivert1], tc->mesh->binormals[tc->dead_idxs[i]]);
						if (tc->mesh->tangents)
							copyVec3(tc->mesh->tangents[e->ivert1], tc->mesh->tangents[tc->dead_idxs[i]]);
						if (tc->mesh->colors)
							tc->mesh->colors[tc->dead_idxs[i]] = tc->mesh->colors[e->ivert1];
						break;
					}
				}
				else
				{
					if (nearSameVec2(tc->mesh->tex1s[e->ivert1], tc->mesh->tex1s[tc->dead_idxs[i]]))
					{
						// copy texcoord attributes from other end of the edge
						copyVec2(&tc->ivertinfos[e->ivert2].attributes[3], &tc->ivertinfos[tc->dead_idxs[i]].attributes[3]);
						copyVec2(tc->mesh->tex1s[e->ivert2], tc->mesh->tex1s[tc->dead_idxs[i]]);
						if (tc->mesh->normals)
							copyVec3(tc->mesh->normals[e->ivert2], tc->mesh->normals[tc->dead_idxs[i]]);
						if (tc->mesh->binormals)
							copyVec3(tc->mesh->binormals[e->ivert2], tc->mesh->binormals[tc->dead_idxs[i]]);
						if (tc->mesh->tangents)
							copyVec3(tc->mesh->tangents[e->ivert2], tc->mesh->tangents[tc->dead_idxs[i]]);
						if (tc->mesh->colors)
							tc->mesh->colors[tc->dead_idxs[i]] = tc->mesh->colors[e->ivert2];
						break;
					}
				}
			}
		}

		eaiSetSize(&tc->dead_idxs, 0);
	}


	for (j = 0; j < eaSize(&sedge->iedges); ++j)
	{
		IEdge *e = sedge->iedges[j];
		int dead_idx, new_idx;

		if (vert_to_use == USE_BEST || vert_to_use == USE_VERT1)
		{
			new_idx = e->ivert1;
			dead_idx = e->ivert2;
		}
		else
		{
			new_idx = e->ivert2;
			dead_idx = e->ivert1;
		}

		addTriRemap(&ri->vremaps, dead_idx, new_idx);
	}

	// mark dead faces
	eaiCopy(&tc->changed_faces, &sedge->faces);
	for (i = 0; i < eaiSize(&tc->changed_faces); i++)
		killTri(tc, tc->changed_faces[i]);
	eaiDestroy(&sedge->faces);
	eaiSetSize(&tc->changed_faces, 0);


	// collect list of changed faces
	for (i = 0; i < eaiSize(&dead_svi->faces); i++)
	{
		DO_DEBUG_ASSERT(!tc->triinfos[dead_svi->faces[i]].is_dead);
		eaiPushUniqueInline(&tc->changed_faces, dead_svi->faces[i]);
	}
	eaiDestroy(&dead_svi->faces);
	dead_count = eaiSize(&tc->changed_faces);

	for (i = 0; i < eaiSize(&new_svi->faces); i++)
	{
		DO_DEBUG_ASSERT(!tc->triinfos[new_svi->faces[i]].is_dead);
		eaiPushUniqueInline(&tc->changed_faces, new_svi->faces[i]);
	}


	// collect list of changed edges and verts
	for (i = 0; i < eaSize(&dead_svi->sedges); i++)
	{
		SVertInfo *svi;
		eaPushUnique(&tc->changed_edges, dead_svi->sedges[i]);

		svi = tc->svertinfos[dead_svi->sedges[i]->svert1];
		for (j = 0; j < eaiSize(&svi->iverts); j++)
			eaiPushUniqueInline(&tc->changed_iverts, svi->iverts[j]);

		svi = tc->svertinfos[dead_svi->sedges[i]->svert2];
		for (j = 0; j < eaiSize(&svi->iverts); j++)
			eaiPushUniqueInline(&tc->changed_iverts, svi->iverts[j]);
	}

	for (i = 0; i < eaSize(&new_svi->sedges); i++)
	{
		SVertInfo *svi;
		eaPushUnique(&tc->changed_edges, new_svi->sedges[i]);

		svi = tc->svertinfos[new_svi->sedges[i]->svert1];
		for (j = 0; j < eaiSize(&svi->iverts); j++)
			eaiPushUniqueInline(&tc->changed_iverts, svi->iverts[j]);

		svi = tc->svertinfos[new_svi->sedges[i]->svert2];
		for (j = 0; j < eaiSize(&svi->iverts); j++)
			eaiPushUniqueInline(&tc->changed_iverts, svi->iverts[j]);
	}
	for (i = 0; i < eaiSize(&dead_svi->iverts); ++i)
		eaiFindAndRemoveFastInline(&tc->changed_iverts, dead_svi->iverts[i]);
	DO_DEBUG_ASSERT(eaFind(&tc->changed_edges, sedge) >= 0);

	// queue tri verts to remap
	for (i = 0; i < dead_count; i++)
	{
		GTriIdx *tri = &tc->mesh->tris[tc->changed_faces[i]];
		DO_DEBUG_ASSERT(!tc->triinfos[tc->changed_faces[i]].is_dead);
		for (j = 0; j < eaSize(&sedge->iedges); j++)
		{
			IEdge *e = sedge->iedges[j];
			int dead_idx, new_idx;
			if (vert_to_use == USE_BEST || vert_to_use == USE_VERT1)
			{
				new_idx = e->ivert1;
				dead_idx = e->ivert2;
			}
			else
			{
				new_idx = e->ivert2;
				dead_idx = e->ivert1;
			}

			if (gmeshTrisMatch(tri, dead_idx, new_idx))
			{
				eaiPush(&tc->new_idxs, new_idx);
				eaiPush(&tc->dead_idxs, dead_idx);
				break;
			}
		}
		if (j < eaSize(&sedge->iedges))
			continue;

		for (j = 0; j < eaSize(&sedge->iedges); j++)
		{
			IEdge *e = sedge->iedges[j];
			int dead_idx, new_idx;

			if (vert_to_use == USE_BEST || vert_to_use == USE_VERT1)
			{
				new_idx = e->ivert1;
				dead_idx = e->ivert2;
			}
			else
			{
				new_idx = e->ivert2;
				dead_idx = e->ivert1;
			}

			if (gmeshTrisMatch1(tri, dead_idx))
			{
				eaiPush(&tc->new_idxs, new_idx);
				eaiPush(&tc->dead_idxs, dead_idx);
				break;
			}
		}
		if (j < eaSize(&sedge->iedges))
			continue;

		DO_DEBUG_ASSERT(vert_to_use != USE_BEST);

		eaiPush(&tc->new_idxs, -1);
		eaiPush(&tc->dead_idxs, -1);
	}

	// destroy changed edges (sedge is destroyed here)
	for (i = 0; i < eaSize(&tc->changed_edges); i++)
		removeEdge(tc, tc->changed_edges[i]);
	sedge = NULL;

	// remap tri verts
	DO_DEBUG_ASSERT(eaiSize(&tc->dead_idxs) == dead_count);
	DO_DEBUG_ASSERT(eaiSize(&tc->new_idxs) == dead_count);
	for (i = 0; i < dead_count; i++)
	{
		GTriIdx *tri = &tc->mesh->tris[tc->changed_faces[i]];
		DO_DEBUG_ASSERT(!tc->triinfos[tc->changed_faces[i]].is_dead);
		if (tc->dead_idxs[i] >= 0)
		{
			eaiFindAndRemoveFastInline(&dead_svi->iverts, tc->dead_idxs[i]);
			tc->ivertinfos[tc->dead_idxs[i]].is_dead = 1;
			remapTriVert(tri, tc->dead_idxs[i], tc->new_idxs[i]);
		}
	}

	combineSVerts(tc, dead_svi, new_svi, (vert_to_use == USE_BEST) ? NULL : (&ri->vremaps));
	if (vert_to_use == USE_BEST)
		changeSVert(tc, new_svi, best, &ri->vremaps);

	// get new tri info
	for (i = 0; i < eaiSize(&tc->changed_faces); i++)
		calculateTriInfo(tc, tc->changed_faces[i], 0);

	if (vert_to_use != USE_BEST)
	{
		new_svi->is_locked = tc->is_terrain && new_svi->on_boundary;
		if (new_svi->is_locked)
		{
			Vec2 v[2];
			int idx = 0;
			for (j = 0; idx < 2 && j < eaSize(&new_svi->sedges); ++j)
			{
				sedge = new_svi->sedges[j];
				if (!(eaSize(&sedge->iedges) == 1 && eaiSize(&sedge->faces) == 1))
					continue;
				if (new_svi == tc->svertinfos[sedge->svert1])
					setVec2(v[idx], tc->svertinfos[sedge->svert2]->v[0] - new_svi->v[0], tc->svertinfos[sedge->svert2]->v[2] - new_svi->v[2]);
				else
					setVec2(v[idx], tc->svertinfos[sedge->svert1]->v[0] - new_svi->v[0], tc->svertinfos[sedge->svert1]->v[2] - new_svi->v[2]);
				++idx;
			}
			if (idx == 2)
			{
				normalVec2(v[0]);
				normalVec2(v[1]);
				new_svi->is_locked = -dotVec2(v[0], v[1]) < 0.9f;
			}
			else
			{
				new_svi->is_locked = 0;
			}
		}
	}

	// debug check
	if (DEBUG_CHECKS)
	{
		checkVerts(tc);
		checkEdges(tc);
		checkTris(tc);
	}


	// recalculate costs
	computeQuadrics(tc, old_cost_val, old_bucket);

	return ri;
}

SEdge *tcGetCheapestEdge(TriCutType *tc)
{
	int i, total_count = 0;
	SEdge *e = NULL;

	++tc->collapse_counter;

	for (i = 0; i < BUCKET_COUNT-1; ++i)
	{
		SEdgeBucket *bucket = &tc->edge_buckets[i];
		int count = eaSize(&bucket->super_edges);
		total_count += count;
		if (!e && count > 0)
		{
#if NO_SORTING
			U32 cheapest_cost = bucket->super_edge_costs[0];
			int j;
			e = bucket->super_edges[0];
			if (i > 0)
			{
				// no need to look in bucket 0, they all have the same cost
				for (j = 1; j < count; ++j)
				{
					if (bucket->super_edge_costs[j] < cheapest_cost)
					{
						cheapest_cost = bucket->super_edge_costs[j];
						e = bucket->super_edges[j];
					}
				}
			}
#else
			e = bucket->super_edges[0];
#endif
		}
	}

	if (total_count < 4)
		e = NULL;

	if (e && e->bucket_index > 0 && e->bucket_index < BUCKET_COUNT-1)
	{
		bool need_rebucket = false;
		for (i = 1; i < BUCKET_COUNT-1; ++i)
		{
			int count = eaSize(&tc->edge_buckets[i].super_edges);
			if (count > total_count / 8)
			{
				need_rebucket = true;
				break;
			}
		}

		if (need_rebucket)
		{
			if (tc->last_need_rebucket == tc->collapse_counter - 1)
				need_rebucket = false;
			tc->last_need_rebucket = tc->collapse_counter;
		}

		if (need_rebucket)
			updateAllBuckets(tc);
	}

	return e;
}

//////////////////////////////////////////////////////////////////////////
// terrain mesh functions

#define TERRAIN_ANGLE_HYSTERESIS_AMOUNT 0.3f
#define FACE_NORMAL_MAX 0.5735f // 55 degrees
#define FACE_NORMAL_MIN 0.342f  // 70 degrees
#define FACE_NORMAL_DOT_MUL (1.f/(FACE_NORMAL_MAX - FACE_NORMAL_MIN))

enum {
	NORM_X = 1,
	NORM_Y = 2,
	NORM_Z = 4
};

#define CHECK_NEIGHBOR_YXZ(new_dir, dir2)			\
	if ((new_dir & NORM_Y) && (dir2 == NORM_Y)) {	\
		new_dir = NORM_Y;							\
	} else if ((new_dir & (NORM_X | NORM_Z)) && ((dir2 & NORM_Y) == 0)) { \
		new_dir &= (NORM_X | NORM_Z);				\
	}												\
	assert(new_dir != 0);

#define CHECK_NEIGHBOR_XZ(new_dir, dir2)			\
	if (dir2 == NORM_X || dir2 == NORM_Z) {			\
		new_dir = dir2;								\
	}

static const bool single_dir[] = {	false,							// 0,
									true,							// NORM_X,
									true, false,					// NORM_Y, NORM_X|NORM_Y,
									true, false, false, false };	// NORM_Z, NORM_X|NORM_Z, NORM_Y|NORM_Z, NORM_X|NORM_Y|NORM_Z

static const int dir_swap[] = { 0,				// 0,
								0,				// NORM_X,
								1, 1,			// NORM_Y, NORM_X|NORM_Y,
								2, 0, 1, 1 };	// NORM_Z, NORM_X|NORM_Z, NORM_Y|NORM_Z, NORM_X|NORM_Y|NORM_Z

#define NORMAL_THRESHOLD 0.707f		// roughly 45 degrees

U8 tmeshClassifyNormal(const Vec3 normal)
{
	U8 dir = 0;
	if (fabs(normal[0]) > NORMAL_THRESHOLD - TERRAIN_ANGLE_HYSTERESIS_AMOUNT)
		dir |= NORM_X;
	if (fabs(normal[1]) > NORMAL_THRESHOLD - TERRAIN_ANGLE_HYSTERESIS_AMOUNT)
		dir |= NORM_Y;
	if (fabs(normal[2]) > NORMAL_THRESHOLD - TERRAIN_ANGLE_HYSTERESIS_AMOUNT)
		dir |= NORM_Z;
	if (!dir) // this handles the case of a triangle made of colinear points
		dir = NORM_Y;
	return dir;
}

#define T_MESH_PROJ_SCALE (1.0/256.0)	// 1 over length of terrain patch

void tmeshCalcTexcoord(Vec2 texcoord, F32 x, F32 y, F32 z, U8 dir)
{
	if (dir == 0)
	{
		// X-axis projection
		setVec2(texcoord, z * T_MESH_PROJ_SCALE * -1, y * T_MESH_PROJ_SCALE);
	}
	else if (dir == 1)
	{
		// Y-axis projection
		setVec2(texcoord, x * T_MESH_PROJ_SCALE * -1, z * T_MESH_PROJ_SCALE);
	}
	else
	{
		// Z-axis projection
		setVec2(texcoord, x * T_MESH_PROJ_SCALE * -1, y * T_MESH_PROJ_SCALE);
	}
}

void tmeshCalcNormal(Vec3 normal, const Vec3 *all_face_normals[3][3], int fidx, int x, int z, int p, int k, int size)
{
	// to get the n value for the verts of each face:
	static const int vn[]  = { 3, 5, 1, 0, 2, 4 };

	// to get the x and y offsets and p value for the faces around a vert:
	static const int fdx[] = {-1, -1, -1,  0,  0,  0};
	static const int fdy[] = {-1, -1,  0,  0,  0, -1};
	static const int fp[]  = { 1,  0,  1,  0,  1,  0};

	F32 mul, normal_weights[ARRAY_SIZE(fdx)];
	const Vec3 *face_normals = all_face_normals[1][1];
	const F32 *last_normal;

	int n, fn = vn[k + p*3]; // which face this is in the neighbor list for this vert

	// calc vertex normal by going around the neighbors until there is an edge with a large normal difference
	last_normal = face_normals[fidx];
	mul = normal_weights[fn] = 1;
	for (n = 1; n < ARRAY_SIZE(fdx); ++n)
	{
		int nn = (fn + n) % ARRAY_SIZE(fdx);
		int nx = x + fdx[nn];
		int nz = z + fdy[nn];
		int np = fp[nn];
		int nidx = (nz * (size-1) + nx) * 2 + np;
		const F32 *this_normal = NULL;

		if (nx >= 0 && nx < size-1 && nz >= 0 && nz < size-1)
		{
			this_normal = face_normals[nidx];
		}
		else if (nx < 0)
		{
			if (nz < 0)
				this_normal = all_face_normals[0][0]?all_face_normals[0][0][np]:NULL;
			else if (nz >= size-1)
				this_normal = all_face_normals[2][0]?all_face_normals[2][0][np]:NULL;
			else
				this_normal = all_face_normals[1][0]?all_face_normals[1][0][nz * 2 + np]:NULL;
		}
		else if (nx >= size-1)
		{
			if (nz < 0)
				this_normal = all_face_normals[0][2]?all_face_normals[0][2][np]:NULL;
			else if (nz >= size-1)
				this_normal = all_face_normals[2][2]?all_face_normals[2][2][np]:NULL;
			else
				this_normal = all_face_normals[1][2]?all_face_normals[1][2][nz * 2 + np]:NULL;
		}
		else if (nz < 0)
		{
			this_normal = all_face_normals[0][1]?all_face_normals[0][1][nx * 2 + np]:NULL;
		}
		else if (nz >= size-1)
		{
			this_normal = all_face_normals[2][1]?all_face_normals[2][1][nx * 2 + np]:NULL;
		}

		if (this_normal)
		{
			mul *= saturate(FACE_NORMAL_DOT_MUL * (dotVec3(last_normal, this_normal) - FACE_NORMAL_MIN));
			last_normal = this_normal;
		}

		normal_weights[nn] = mul;
	}

	// now go the other direction
	copyVec3(face_normals[fidx], normal);
	last_normal = face_normals[fidx];
	mul = 1;
	for (n = 1; n < ARRAY_SIZE(fdx); ++n)
	{
		int nn = (ARRAY_SIZE(fdx) + fn - n) % ARRAY_SIZE(fdx);
		int nx = x + fdx[nn];
		int nz = z + fdy[nn];
		int np = fp[nn];
		int nidx = (nz * (size-1) + nx) * 2 + np;
		const F32 *this_normal = NULL;

		if (nx >= 0 && nx < size-1 && nz >= 0 && nz < size-1)
		{
			this_normal = face_normals[nidx];
		}
		else if (nx < 0)
		{
			if (nz < 0)
				this_normal = all_face_normals[0][0]?all_face_normals[0][0][np]:NULL;
			else if (nz >= size-1)
				this_normal = all_face_normals[2][0]?all_face_normals[2][0][np]:NULL;
			else
				this_normal = all_face_normals[1][0]?all_face_normals[1][0][nz * 2 + np]:NULL;
		}
		else if (nx >= size-1)
		{
			if (nz < 0)
				this_normal = all_face_normals[0][2]?all_face_normals[0][2][np]:NULL;
			else if (nz >= size-1)
				this_normal = all_face_normals[2][2]?all_face_normals[2][2][np]:NULL;
			else
				this_normal = all_face_normals[1][2]?all_face_normals[1][2][nz * 2 + np]:NULL;
		}
		else if (nz < 0)
		{
			this_normal = all_face_normals[0][1]?all_face_normals[0][1][nx * 2 + np]:NULL;
		}
		else if (nz >= size-1)
		{
			this_normal = all_face_normals[2][1]?all_face_normals[2][1][nx * 2 + np]:NULL;
		}

		if (this_normal)
		{
			mul *= saturate(FACE_NORMAL_DOT_MUL * (dotVec3(last_normal, this_normal) - FACE_NORMAL_MIN));
			last_normal = this_normal;
		}

		MAX1(normal_weights[nn], mul);
		scaleAddVec3(last_normal, normal_weights[nn], normal, normal);
	}

	normalVec3(normal);
}

static void tcmeshCalcNormal(Vec3 normal, TriCutType *tc, int fidx, int vidx, F32 face_normal_min, F32 face_normal_max)
{
	GTriIdx *tri = &tc->mesh->tris[fidx];
	SVertInfo *svert = tc->ivertinfos[tri->idx[vidx]].svert;
	int *neighbor_faces[2] = {NULL, NULL}, i, j, didx;
	const F32 *last_normal;
	F32 *normal_weights, mul, last_area;
	bool found, full_circle = false;
	F32 face_normal_dot_mul = 1.f / (face_normal_max - face_normal_min);

	assert(face_normal_min != face_normal_max);

	// create a sorted neighbor face list for this vertex with this face at the head of the list
	didx = 0;
	eaiPush(&neighbor_faces[0], fidx);
	eaiPush(&neighbor_faces[1], fidx);
	ANALYSIS_ASSUME(neighbor_faces[0] && neighbor_faces[1]);

	do 
	{
		found = false;

		while (didx < 2 && !found)
		{
			int face = neighbor_faces[didx][eaiSize(&neighbor_faces[didx]) - 1];
			for (i = 0; i < ARRAY_SIZE(tc->triinfos[face].sedges) && !found; ++i)
			{
				SEdge *sedge = tc->triinfos[face].sedges[i];
				if (!sedge)
					continue;
				if (eaFind(&svert->sedges, sedge) >= 0)
				{
					for (j = 0; j < eaiSize(&sedge->faces) && !found; ++j)
					{
						if (didx==0 && eaiSize(&neighbor_faces[0]) >= 3 && sedge->faces[j] == fidx)
							full_circle = true;
						if (eaiFind(&neighbor_faces[didx], sedge->faces[j]) < 0 && 
							(didx == 0 || eaiFind(&neighbor_faces[didx-1], sedge->faces[j]) < 0))
						{
							eaiPush(&neighbor_faces[didx], sedge->faces[j]);
							found = true;
						}
					}
				}
			}

			if (!found)
			{
				++didx;
			}
		}

	} while (found);

	normal_weights = ScratchAlloc((eaiSize(&neighbor_faces[0]) + eaiSize(&neighbor_faces[1])) * sizeof(F32));

	// calc vertex normal by going around the neighbors until there is an edge with a large normal difference
	last_normal = tc->triinfos[fidx].face_normal;
	last_area = tc->triinfos[fidx].area;
	mul = normal_weights[0] = normal_weights[eaiSize(&neighbor_faces[0])] = 1;
	for (i = 1; i < eaiSize(&neighbor_faces[0]); ++i)
	{
		int nidx = neighbor_faces[0][i];

		F32 normal_mul = saturate(face_normal_dot_mul * (dotVec3(last_normal, tc->triinfos[nidx].face_normal) - face_normal_min));
		F32 area_ratio = saturate(tc->triinfos[nidx].area / last_area);
		mul *= lerp(area_ratio * normal_mul, 1, normal_mul);
		last_normal = tc->triinfos[nidx].face_normal;
		last_area = tc->triinfos[nidx].area;

		normal_weights[i] = mul;
	}

	// now go the other direction
	didx = eaiSize(&neighbor_faces[1]) > 1 || !full_circle;
	last_normal = tc->triinfos[fidx].face_normal;
	last_area = tc->triinfos[fidx].area;
	mul = 1;
	for (i = 1; i < eaiSize(&neighbor_faces[didx]); ++i)
	{
		int ni = (didx == 1) ? i : (eaiSize(&neighbor_faces[0]) - i);
		int nidx = neighbor_faces[didx][ni];
		int nn = (didx == 1) ? (eaiSize(&neighbor_faces[0]) + i) : ni;

		F32 normal_mul = saturate(face_normal_dot_mul * (dotVec3(last_normal, tc->triinfos[nidx].face_normal) - face_normal_min));
		F32 area_ratio = saturate(tc->triinfos[nidx].area / last_area);
		mul *= lerp(area_ratio * normal_mul, 1, normal_mul);
		last_normal = tc->triinfos[nidx].face_normal;
		last_area = tc->triinfos[nidx].area;

		MAX1(normal_weights[nn], mul);
	}

	copyVec3(tc->triinfos[fidx].face_normal, normal);
	for (i = 1; i < eaiSize(&neighbor_faces[0]); ++i)
	{
		int nidx = neighbor_faces[0][i];
		scaleAddVec3(tc->triinfos[nidx].face_normal, normal_weights[i], normal, normal);
	}
	for (i = 1; i < eaiSize(&neighbor_faces[1]); ++i)
	{
		int nidx = neighbor_faces[1][i];
		int nn = eaiSize(&neighbor_faces[0]) + i;
		scaleAddVec3(tc->triinfos[nidx].face_normal, normal_weights[nn], normal, normal);
	}
	normalVec3(normal);

	ScratchFree(normal_weights);
	eaiDestroy(&neighbor_faces[0]);
	eaiDestroy(&neighbor_faces[1]);
}

void tmeshFixupFaceClassifications(U8 *face_buffer, int size)
{
	int i, j, p, dir, dir2, dir3;
	bool found;

	// Sort between Y and X/Z
	do
	{
		found = false;
		for (i = 0; i < (size-1); i++)
		{
			for (j = 0; j < (size-1); j++)
			{
				for (p=0; p<2; ++p)
				{
					dir = face_buffer[(i + j * (size-1)) * 2 + p];
					// Only handle "gray" colors
					dir3 = dir;
					if (dir != NORM_X && dir != NORM_Y && dir != NORM_Z)
					{
						if (p == 0)
						{
							dir2 = face_buffer[(i + j * (size-1)) * 2 + 1];
							CHECK_NEIGHBOR_YXZ(dir3, dir2);
							if (i > 0)
							{
								dir2 = face_buffer[(i-1 + j * (size-1)) * 2 + 1];
								CHECK_NEIGHBOR_YXZ(dir3, dir2);
							}
							if (j < (size-2))
							{
								dir2 = face_buffer[(i + (j+1) * (size-1)) * 2 + 1];
								CHECK_NEIGHBOR_YXZ(dir3, dir2);
							}
						}
						else
						{
							dir2 = face_buffer[(i + j * (size-1)) * 2];
							CHECK_NEIGHBOR_YXZ(dir3, dir2);
							if (i < (size-2))
							{
								dir2 = face_buffer[(i+1 + j * (size-1)) * 2];
								CHECK_NEIGHBOR_YXZ(dir3, dir2);
							}
							if (j > 0)
							{
								dir2 = face_buffer[(i + (j-1) * (size-1)) * 2];
								CHECK_NEIGHBOR_YXZ(dir3, dir2);
							}
						}
					}
					if (dir != dir3)
					{
						face_buffer[(i + j * (size-1)) * 2 + p] = dir3;
						found = true;
					}
				}
			}
		}
	} while (found);

	// Catch any remaining
	for (i = 0; i < (size-1); i++)
	{
		for (j = 0; j < (size-1); j++)
		{
			for (p=0; p<2; ++p)
			{
				dir = face_buffer[(i + j * (size-1)) * 2 + p];
				// Only handle "gray" colors
				if (dir != NORM_X && dir != NORM_Y && dir != NORM_Z && dir != (NORM_X | NORM_Z))
				{
					face_buffer[(i + j * (size-1)) * 2 + p] = NORM_Y;
				}
			}
		}
	}

	// Sort between X and Z
	do
	{
		found = false;
		for (i = 0; i < (size-1); i++)
		{
			for (j = 0; j < (size-1); j++)
			{
				for (p=0; p<2; ++p)
				{
					dir = face_buffer[(i + j * (size-1)) * 2 + p];
					// Only handle "gray" colors
					dir3 = dir;
					if (dir == (NORM_X | NORM_Z))
					{
						if (p == 0)
						{
							dir2 = face_buffer[(i + j * (size-1)) * 2 + 1];
							CHECK_NEIGHBOR_XZ(dir3, dir2);
							if (i > 0)
							{
								dir2 = face_buffer[(i-1 + j * (size-1)) * 2 + 1];
								CHECK_NEIGHBOR_XZ(dir3, dir2);
							}
							if (j < (size-2))
							{
								dir2 = face_buffer[(i + (j+1) * (size-1)) * 2 + 1];
								CHECK_NEIGHBOR_XZ(dir3, dir2);
							}
						}
						else
						{
							dir2 = face_buffer[(i + j * (size-1)) * 2];
							CHECK_NEIGHBOR_XZ(dir3, dir2);
							if (i < (size-2))
							{
								dir2 = face_buffer[(i+1 + j * (size-1)) * 2];
								CHECK_NEIGHBOR_XZ(dir3, dir2);
							}
							if (j > 0)
							{
								dir2 = face_buffer[(i + (j-1) * (size-1)) * 2];
								CHECK_NEIGHBOR_XZ(dir3, dir2);
							}
						}
					}

					if (dir != dir3)
					{
						face_buffer[(i + j * (size-1)) * 2 + p] = dir3;
						found = true;
					}
				}
			}
		}
	} while (found);

	for (i = 0; i < (size-1)*(size-1)*2; ++i)
	{
		dir = face_buffer[i];
		assert(dir > 0 || dir < ARRAY_SIZE(dir_swap));
		face_buffer[i] = dir_swap[dir];
	}
}

__forceinline static U8 determineFacing(const Vec3 texvec, const Vec3 face_normal)
{
	U8 normal_facing = tmeshClassifyNormal(face_normal);
	U8 facingidxs[3] = {0,1,2};
	int i, j;

	// if face normal is strongly facing a single direction, use that mapping
	if (single_dir[normal_facing])
		return dir_swap[normal_facing];

	// sort by texvec strength
	for (i = 0; i < 3; ++i)
	{
		for (j = i+1; j < 3; ++j)
		{
			if (texvec[facingidxs[j]] > texvec[facingidxs[i]])
			{
				U8 temp = facingidxs[i];
				facingidxs[i] = facingidxs[j];
				facingidxs[j] = temp;
			}
		}
	}

	// if strongest parent facing is much bigger than second strongest, just use it
	if (texvec[facingidxs[0]] > 1.5f * texvec[facingidxs[1]])
		return facingidxs[0];

	// if strongest parent facing direction is in the face normal, use it
	if (facingidxs[0] == 0 && (normal_facing & NORM_X) ||
		facingidxs[0] == 1 && (normal_facing & NORM_Y) ||
		facingidxs[0] == 2 && (normal_facing & NORM_Z))
	{
		return facingidxs[0];
	}

	// if second strongest parent facing direction is in the face normal, use it
	if (facingidxs[1] == 0 && (normal_facing & NORM_X) ||
		facingidxs[1] == 1 && (normal_facing & NORM_Y) ||
		facingidxs[1] == 2 && (normal_facing & NORM_Z))
	{
		return facingidxs[1];
	}

	// give up, use face normal facing
	return dir_swap[normal_facing];
}

void applyTerrainTexturingAndNormals(TriCutType *tc, GMesh *output, Vec3 *texmap, int texmap_res, F32 one_over_texmap_step MEM_DBG_PARMS)
{
	F32 face_normal_min, face_normal_max;
	int i, x, y, tri_count = tc->mesh->tri_count;
	VertexHash vhash = {0};
	bool add_normals = true;//!(tc->mesh->usagebits & USE_NORMALS);
	F32 size = MAX((tc->bounds_max[0] - tc->bounds_min[0]), (tc->bounds_max[2] - tc->bounds_min[2]));
	F32 angle_min, angle_max;

	if (size <= 256)
	{
		angle_min = 65;
		angle_max = 75;
	}
	else if (size <= 512)
	{
		angle_min = 85;
		angle_max = 90;
	}
	else if (size <= 1024)
	{
		angle_min = 88;
		angle_max = 90;
	}
	else
	{
		angle_min = 89;
		angle_max = 90;
	}

	face_normal_min = cosf(RAD(angle_max));
	face_normal_max = cosf(RAD(angle_min));

	assert(tc->mesh->positions);

	for (i = 0; i < tri_count; ++i)
	{
		GTriIdx *tri = &tc->mesh->tris[i];
		if (tc->triinfos[i].is_dead || isTriDegenerate(tc, tri))
			continue;
		copyVec3(tc->ivertinfos[tri->idx[0]].svert->v, tc->mesh->positions[tri->idx[0]]);
		copyVec3(tc->ivertinfos[tri->idx[1]].svert->v, tc->mesh->positions[tri->idx[1]]);
		copyVec3(tc->ivertinfos[tri->idx[2]].svert->v, tc->mesh->positions[tri->idx[2]]);
	}

	// cribbed from gmeshMerge
	gmeshSetUsageBits_dbg(output, (tc->mesh->usagebits | USE_TEX1S | USE_NORMALS)
						  MEM_DBG_PARMS_CALL);
	gmeshEnsureTrisFit_dbg(output, tc->live_tris
						   MEM_DBG_PARMS_CALL);
	gmeshSetVarColorSize_dbg(output, tc->mesh->varcolor_size
							 MEM_DBG_PARMS_CALL);

	for (i = 0; i < tri_count; ++i)
	{
		int idx0, idx1, idx2;
		GTriIdx *srctri = &tc->mesh->tris[i];
		int i0 = srctri->idx[0], i1 = srctri->idx[1], i2 = srctri->idx[2];
		Vec3 normal0, normal1, normal2;
		Vec2 texcoord0, texcoord1, texcoord2;
		U8 facing;
		Vec2 p0, p1, p2;
		IVec2 trimin, trimax;
		Vec3 texvec;

		if (tc->triinfos[i].is_dead || isTriDegenerate(tc, srctri))
			continue;

		// calculate facing direction from original mesh's texture mapping direction map
		setVec2(p0, tc->mesh->positions[i0][0] * one_over_texmap_step, tc->mesh->positions[i0][2] * one_over_texmap_step);
		setVec2(p1, tc->mesh->positions[i1][0] * one_over_texmap_step, tc->mesh->positions[i1][2] * one_over_texmap_step);
		setVec2(p2, tc->mesh->positions[i2][0] * one_over_texmap_step, tc->mesh->positions[i2][2] * one_over_texmap_step);

		trimin[0] = imin3(p0[0], p1[0], p2[0]);
		trimin[0] = CLAMP(trimin[0], 0, texmap_res-1);

		trimin[1] = imin3(p0[1], p1[1], p2[1]);
		trimin[1] = CLAMP(trimin[1], 0, texmap_res-1);

		trimax[0] = imax3(p0[0], p1[0], p2[0]);
		trimax[0] = CLAMP(trimax[0], 0, texmap_res-1);

		trimax[1] = imax3(p0[1], p1[1], p2[1]);
		trimax[1] = CLAMP(trimax[1], 0, texmap_res-1);

		// loop over part of texmap that overlaps the triangle
		zeroVec3(texvec);
		for (y = trimin[1]; y <= trimax[1]; ++y)
		{
			for (x = trimin[0]; x <= trimax[0]; ++x)
			{
				Vec2 clip_min, clip_max;
				F32 scale;
				setVec2(clip_min, x, y);
				addVec2same(clip_min, 1, clip_max);
				scale = findClippedArea2D(p0, p1, p2, clip_min, clip_max);
				scaleAddVec3(texmap[y * texmap_res + x], scale, texvec, texvec);
			}
		}

		facing = determineFacing(texvec, tc->triinfos[i].face_normal);

		tmeshCalcTexcoord(texcoord0, tc->mesh->positions[i0][0], tc->mesh->positions[i0][1], tc->mesh->positions[i0][2], facing);
		tmeshCalcTexcoord(texcoord1, tc->mesh->positions[i1][0], tc->mesh->positions[i1][1], tc->mesh->positions[i1][2], facing);
		tmeshCalcTexcoord(texcoord2, tc->mesh->positions[i2][0], tc->mesh->positions[i2][1], tc->mesh->positions[i2][2], facing);

		if (add_normals)
		{
			tcmeshCalcNormal(normal0, tc, i, 0, face_normal_min, face_normal_max);
			tcmeshCalcNormal(normal1, tc, i, 1, face_normal_min, face_normal_max);
			tcmeshCalcNormal(normal2, tc, i, 2, face_normal_min, face_normal_max);
		}
		else
		{
			copyVec3(tc->mesh->normals[i0], normal0);
			normalVec3(normal0);
			copyVec3(tc->mesh->normals[i1], normal1);
			normalVec3(normal1);
			copyVec3(tc->mesh->normals[i2], normal2);
			normalVec3(normal2);
		}

		idx0 = gmeshAddVertInternal(output,
			tc->mesh->positions?tc->mesh->positions[i0]:0,
			tc->mesh->positions2?tc->mesh->positions2[i0]:0,
			normal0,
			tc->mesh->normals2?tc->mesh->normals2[i0]:0,
			tc->mesh->binormals?tc->mesh->binormals[i0]:0,
			tc->mesh->tangents?tc->mesh->tangents[i0]:0,
			texcoord0,
			tc->mesh->tex2s?tc->mesh->tex2s[i0]:0,
			tc->mesh->colors?&tc->mesh->colors[i0]:0,
			tc->mesh->varcolors?&tc->mesh->varcolors[i0*tc->mesh->varcolor_size]:0,
			tc->mesh->boneweights?tc->mesh->boneweights[i0]:0,
			tc->mesh->bonemats?tc->mesh->bonemats[i0]:0,
			true, &vhash, 0, true, true);
		if (tc->mesh->varcolors && memcmp(&tc->mesh->varcolors[i0*tc->mesh->varcolor_size], &output->varcolors[idx0*output->varcolor_size], output->varcolor_size)!=0)
			VarColorAverage(&tc->mesh->varcolors[i0*tc->mesh->varcolor_size], &output->varcolors[idx0*output->varcolor_size], &output->varcolors[idx0*output->varcolor_size], output->varcolor_size);
		idx1 = gmeshAddVertInternal(output,
			tc->mesh->positions?tc->mesh->positions[i1]:0,
			tc->mesh->positions2?tc->mesh->positions2[i1]:0,
			normal1,
			tc->mesh->normals2?tc->mesh->normals2[i1]:0,
			tc->mesh->binormals?tc->mesh->binormals[i1]:0,
			tc->mesh->tangents?tc->mesh->tangents[i1]:0,
			texcoord1,
			tc->mesh->tex2s?tc->mesh->tex2s[i1]:0,
			tc->mesh->colors?&tc->mesh->colors[i1]:0,
			tc->mesh->varcolors?&tc->mesh->varcolors[i1*tc->mesh->varcolor_size]:0,
			tc->mesh->boneweights?tc->mesh->boneweights[i1]:0,
			tc->mesh->bonemats?tc->mesh->bonemats[i1]:0,
			true, &vhash, 0, true, true);
		if (tc->mesh->varcolors && memcmp(&tc->mesh->varcolors[i1*tc->mesh->varcolor_size], &output->varcolors[idx1*output->varcolor_size], output->varcolor_size)!=0)
			VarColorAverage(&tc->mesh->varcolors[i1*tc->mesh->varcolor_size], &output->varcolors[idx1*output->varcolor_size], &output->varcolors[idx1*output->varcolor_size], output->varcolor_size);
		idx2 = gmeshAddVertInternal(output,
			tc->mesh->positions?tc->mesh->positions[i2]:0,
			tc->mesh->positions2?tc->mesh->positions2[i2]:0,
			normal2,
			tc->mesh->normals2?tc->mesh->normals2[i2]:0,
			tc->mesh->binormals?tc->mesh->binormals[i2]:0,
			tc->mesh->tangents?tc->mesh->tangents[i2]:0,
			texcoord2,
			tc->mesh->tex2s?tc->mesh->tex2s[i2]:0,
			tc->mesh->colors?&tc->mesh->colors[i2]:0,
			tc->mesh->varcolors?&tc->mesh->varcolors[i2*tc->mesh->varcolor_size]:0,
			tc->mesh->boneweights?tc->mesh->boneweights[i2]:0,
			tc->mesh->bonemats?tc->mesh->bonemats[i2]:0,
			true, &vhash, 0, true, true);
		if (tc->mesh->varcolors && memcmp(&tc->mesh->varcolors[i2*tc->mesh->varcolor_size], &output->varcolors[idx2*output->varcolor_size], output->varcolor_size)!=0)
			VarColorAverage(&tc->mesh->varcolors[i2*tc->mesh->varcolor_size], &output->varcolors[idx2*output->varcolor_size], &output->varcolors[idx2*output->varcolor_size], output->varcolor_size);
		gmeshAddTri(output, idx0, idx1, idx2, facing, false);
	}

	freeVertexHashData(&vhash);
}
