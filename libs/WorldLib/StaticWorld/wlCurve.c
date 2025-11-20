/***************************************************************************



***************************************************************************/

#include "ScratchStack.h"
#include "LineDist.h"

#include "wlCurve.h"
#include "WorldGridPrivate.h"

#include "wlCurve_h_ast.c"

AUTO_RUN_ANON(memBudgetAddMapping(__FILE__, BUDGET_World););

typedef struct CurveGeometryDef {
	F32 geometry_length;
} CurveGeometryDef;

// Debug only
void splineValidate(Spline *spline)
{
	int num_points = eafSize(&spline->spline_points)/3;
	assert(eafSize(&spline->spline_points) == num_points*3);
	assert(eafSize(&spline->spline_deltas) == num_points*3);
	assert(eafSize(&spline->spline_up) == num_points*3);
	assert(eaiSize(&spline->spline_geom) == num_points);
	if (!spline->spline_widths)
	{
		eafSetSize(&spline->spline_widths, num_points);
	}
	assert(eafSize(&spline->spline_widths) == num_points);
}

GroupDef **curvesGroupFreeList = NULL; // Curves we want to delete at the next available opportunity

void splineDestroy(Spline *spline)
{
	if (spline->spline_points)
		eafDestroy(&spline->spline_points);
	if (spline->spline_up)
		eafDestroy(&spline->spline_up);
	if (spline->spline_deltas)
		eafDestroy(&spline->spline_deltas);
	if (spline->spline_widths)
		eafDestroy(&spline->spline_widths);
	if (spline->spline_geom)
		ea32Destroy(&spline->spline_geom);
}

void splineInsertCP(Spline *spline, int index, const Vec3 new_point, const Vec3 up, const Vec3 dir, F32 width)
{
	assert(index >= 0 && index <= eafSize(&spline->spline_points)-3);

	splineValidate(spline);
	eafInsert(&spline->spline_points, new_point[0], index);
	eafInsert(&spline->spline_points, new_point[1], index+1);
	eafInsert(&spline->spline_points, new_point[2], index+2);
	eafInsert(&spline->spline_up, up[0], index);
	eafInsert(&spline->spline_up, up[1], index+1);
	eafInsert(&spline->spline_up, up[2], index+2);
	eafInsert(&spline->spline_deltas, dir[0], index);
	eafInsert(&spline->spline_deltas, dir[1], index+1);
	eafInsert(&spline->spline_deltas, dir[2], index+2);
	eafInsert(&spline->spline_widths, width, index/3);
	eaiInsert(&spline->spline_geom, 0, index/3);
	splineValidate(spline);
}

void splineAppendCP(Spline *spline, const Vec3 new_point, const Vec3 up, const Vec3 direction, S32 geometry, F32 width)
{
	splineValidate(spline);
	eafPush(&spline->spline_points, new_point[0]);
	eafPush(&spline->spline_points, new_point[1]);
	eafPush(&spline->spline_points, new_point[2]);
	eafPush(&spline->spline_up, up[0]);
	eafPush(&spline->spline_up, up[1]);
	eafPush(&spline->spline_up, up[2]);
	eafPush(&spline->spline_deltas, direction[0]);
	eafPush(&spline->spline_deltas, direction[1]);
	eafPush(&spline->spline_deltas, direction[2]);
	eafPush(&spline->spline_widths, width);
	eaiPush(&spline->spline_geom, geometry);
	splineValidate(spline);
}

//improper means that the first point will not necessarily face down the z-axis
void splineAppendAutoCP(Spline *spline, const Vec3 new_point, const Vec3 up, bool improper, S32 geometry, F32 width)
{
	// Recompute previous
	int size = eafSize(&spline->spline_points);
	//Vec3 left;
	splineValidate(spline);
	if (size > 5) // at least two prior points
	{
		Vec3 dir;
		subVec3(new_point, &spline->spline_points[size-6], dir);
		normalVec3(dir);
		copyVec3(dir, &spline->spline_deltas[size-3]);
	}
	else if (improper && size > 2)
	{
		Vec3 dir;
		subVec3(new_point, &spline->spline_points[0], dir);
		normalVec3(dir);
		copyVec3(dir, &spline->spline_deltas[0]);
	}

	// Push new point & delta
	eafPush(&spline->spline_points, new_point[0]);
	eafPush(&spline->spline_points, new_point[1]);
	eafPush(&spline->spline_points, new_point[2]);
	eafPush(&spline->spline_up, up[0]);
	eafPush(&spline->spline_up, up[1]);
	eafPush(&spline->spline_up, up[2]);
	eafPush(&spline->spline_widths, width);
	eaiPush(&spline->spline_geom, geometry);
	// Assume endpoint
	if (size > 2)
	{
		Vec3 end_dir, new_delta;
		addVec3(&spline->spline_points[size-3], &spline->spline_deltas[size-3], end_dir);
		subVec3(new_point, end_dir, new_delta);
		normalVec3(new_delta);
		eafPush(&spline->spline_deltas, new_delta[0]);
		eafPush(&spline->spline_deltas, new_delta[1]);
		eafPush(&spline->spline_deltas, new_delta[2]);
	}
	else
	{
		eafPush(&spline->spline_deltas, 1);
		eafPush(&spline->spline_deltas, 0);
		eafPush(&spline->spline_deltas, 0);
	}
	splineValidate(spline);
}

void splineRedrawPoints(Spline *spline)
{
	F32 *temp_spline = NULL;
	F32 *temp_widths = NULL;
	Vec3 off_zero;
	int i;

	splineValidate(spline);

	setVec3(off_zero, 0, 0, 0);
	eafCopy(&temp_spline, &spline->spline_points);
	eafCopy(&temp_widths, &spline->spline_widths);

	eafSetSize(&spline->spline_points, 0);
	eafSetSize(&spline->spline_up, 0);
	eafSetSize(&spline->spline_deltas, 0);
	eafSetSize(&spline->spline_widths, 0);
	eaiSetSize(&spline->spline_geom, 0);
	splineValidate(spline);

	for (i = 0; i < eafSize(&temp_spline); i+= 3)
	{
		splineAppendAutoCP(spline, &temp_spline[i], upvec, true, 0, temp_widths[i/3]);
	}
	splineValidate(spline);
	eafDestroy(&temp_spline);
	eafDestroy(&temp_widths);
}

void splineDeleteCP(Spline *spline, int index)
{
	int i;
	devassertmsg(spline->spline_points, "spline_points must be at least 3 in size");
	splineValidate(spline);
	for (i = index; i < eafSize(&spline->spline_points)-3; i+=3)
	{
		copyVec3(&spline->spline_points[i+3], &spline->spline_points[i]);
		copyVec3(&spline->spline_up[i+3], &spline->spline_up[i]);
		copyVec3(&spline->spline_deltas[i+3], &spline->spline_deltas[i]);
		spline->spline_widths[i/3] = spline->spline_widths[i/3+1];
		spline->spline_geom[i/3] = spline->spline_geom[i/3+1];
	}
	eafPop(&spline->spline_points);
	eafPop(&spline->spline_points);
	eafPop(&spline->spline_points);
	eafPop(&spline->spline_up);
	eafPop(&spline->spline_up);
	eafPop(&spline->spline_up);
	eafPop(&spline->spline_deltas);
	eafPop(&spline->spline_deltas);
	eafPop(&spline->spline_deltas);
	eafPop(&spline->spline_widths);
	eaiPop(&spline->spline_geom);

	splineValidate(spline);
}

F32 splineGetWidth(Spline *spline, int index, F32 t)
{
	splineValidate(spline);
	if (index >= eafSize(&spline->spline_points)-3)
		return spline->spline_widths[eafSize(&spline->spline_widths)-1];
	return lerp(spline->spline_widths[index/3], spline->spline_widths[index/3+1], t);
}

#define SPLINE_SEGMENTS 512

void splineGetMatrices(const Mat4 parent_mat, const	Vec3 curve_center, const F32 curve_scale,
					   Spline *spline, int index, Mat4 *matrices, bool linearize)
{
	Vec3 dir[2];
    Vec3 temp;
	F32 length;
	int offset = linearize ? 0 : 3;

	identityMat4(matrices[0]);

	length = distance3(&spline->spline_points[index], &spline->spline_points[index+3]) * curve_scale;

	copyVec3(&spline->spline_deltas[index], dir[0]);
	normalVec3(dir[0]);

	copyVec3(&spline->spline_deltas[index+offset], dir[1]);
	normalVec3(dir[1]);

	subVec3(&spline->spline_points[index], curve_center, temp);
    scaleVec3(temp, curve_scale, temp);
	addVec3(temp, curve_center, temp);
	mulVecMat4(temp, parent_mat, matrices[0][0]);
	subVec3(&spline->spline_points[index+3], curve_center, temp);
    scaleVec3(temp, curve_scale, temp); 
	addVec3(temp, curve_center, temp);
	mulVecMat4(temp, parent_mat, matrices[0][1]);

    mulVecMat3(&spline->spline_deltas[index], parent_mat, matrices[0][2]);
	scaleVec3(matrices[0][2], 0.33333f*length, matrices[0][2]);
	addVec3(matrices[0][2], matrices[0][0], matrices[0][2]);

    mulVecMat3(&spline->spline_deltas[index+offset], parent_mat, matrices[0][3]);
	scaleVec3(matrices[0][3], 0.33333f*length, matrices[0][3]);
	subVec3(matrices[0][1], matrices[0][3], matrices[0][3]);

    mulVecMat3(&spline->spline_up[index], parent_mat, matrices[1][0]);
    mulVecMat3(&spline->spline_up[index+offset], parent_mat, matrices[1][1]);

	matrices[1][2][1] = 1.f;
}

void splineGetControlPoints(Spline *spline, int index, Vec3 control_points[4])
{
	Vec3 dir;
	F32 length;

	copyVec3(&spline->spline_points[index], control_points[0]);
	copyVec3(&spline->spline_points[index+3], control_points[3]);
	length = distance3(control_points[0], control_points[3]);
	scaleVec3(&spline->spline_deltas[index], 0.33333f*length, dir);
	addVec3(control_points[0], dir, control_points[1]);
	scaleVec3(&spline->spline_deltas[index+3], 0.33333f*length, dir);
	subVec3(control_points[3], dir, control_points[2]);
}

// Evaluates a point on a spline with only one control point
void splineTransformSingle(Spline *spline, int index, F32 weight, const Vec3 in, Vec3 out, Vec3 up, Vec3 tangent)
{
	Mat4 matrices[2];

	assert((index%3) == 0);

	copyVec3(&spline->spline_points[index], matrices[0][0]);
	copyVec3(&spline->spline_points[index], matrices[0][2]);
	addVec3(&spline->spline_deltas[index], matrices[0][2], matrices[0][2]);
	copyVec3(matrices[0][2], matrices[0][1]);
	copyVec3(matrices[0][2], matrices[0][3]);
	copyVec3(&spline->spline_up[index], matrices[1][0]);
	copyVec3(&spline->spline_up[index], matrices[1][1]);

	matrices[1][2][1] = 1.f;

	splineEvaluate(matrices, weight, in, out, up, tangent);
	copyVec3(&spline->spline_deltas[index], tangent);
	normalVec3(tangent);
}

// Evaluates a point on the spline-curved mesh
// Must be kept in sync with the vertex shader
void splineTransform(Spline *spline, int index, F32 weight, const Vec3 in, Vec3 out, Vec3 up, Vec3 tangent, bool linearize)
{
    Mat4 identity;
	Mat4 matrices[2];

	if (index >= eafSize(&spline->spline_points)-3)
	{
		splineTransformSingle(spline, eafSize(&spline->spline_points)-3, 0, in, out, up, tangent);
		return;
	}

    identityMat4(identity);
	splineGetMatrices(identity, zerovec3, 1.f, spline, index, matrices, linearize);

	splineEvaluate(matrices, weight, in, out, up, tangent);
	normalVec3(tangent);
}

void splineGetNextPointEx( Spline *in, bool linearize, const Vec3 offset, int begin_pt, int end_pt, F32 begin_t, F32 end_t, F32 length, int *new_index, F32 *new_t, F32 *exact_length, F32 *length_remainder, Vec3 exact_point, Vec3 exact_up, Vec3 exact_tangent )
{
	int i;
	F32 current_length = 0;
	bool first_point = true;
	Vec3 last_point, last_up, last_tangent;

	for (i = begin_pt; i <= end_pt; i+=3)
	{
		F32 t0 = 0, t1 = 1;
		F32 t;

		if (i == begin_pt) t0 = begin_t;
		if (i == end_pt) t1 = end_t;

		for (t = t0; t <= t1; t += 1.f/SPLINE_SEGMENTS)
		{
			Vec3 cur_point, cur_up, cur_tangent;
			F32 distance = 0, last_length;
			splineTransform(in, i, t, offset, cur_point, cur_up, cur_tangent, linearize);
			last_length = current_length;
			if (!first_point)
			{
				distance = distance3(last_point, cur_point);
				current_length += distance;
			}
			if (current_length >= length)
			{
				F32 exact_t = t;
				if (!first_point && distance > 0)
				{
					// Find intersection t
					F32 interp = (length - last_length) / distance;
					exact_t = t - (1.f/SPLINE_SEGMENTS)*(1-interp);

					scaleVec3(last_point, (1-interp), last_point);
					scaleVec3(cur_point, interp, exact_point);
					addVec3(exact_point, last_point, exact_point);

					scaleVec3(last_up, (1-interp), last_up);
					scaleVec3(cur_up, interp, exact_up);
					addVec3(exact_up, last_up, exact_up);

					scaleVec3(last_tangent, (1-interp), last_tangent);
					scaleVec3(cur_tangent, interp, exact_tangent);
					addVec3(exact_tangent, last_tangent, exact_tangent);
					normalVec3(exact_tangent);
				}
				else
				{
					copyVec3(cur_point, exact_point);
					copyVec3(cur_up, exact_up);
					copyVec3(cur_tangent, exact_tangent);
				}

				*exact_length = length;
				*length_remainder = current_length-length;
				*new_index = i;
				*new_t = t;
				return;
			}
			copyVec3(cur_point, last_point);
			copyVec3(cur_up, last_up);
			copyVec3(cur_tangent, last_tangent);
			first_point = false;
		}
	}

	copyVec3(last_point, exact_point);
	copyVec3(last_up, exact_up);
	copyVec3(last_tangent, exact_tangent);
	*exact_length = current_length;
	*length_remainder = 0;
	*new_index = end_pt;
	*new_t = end_t;
}

F32 splineGetNextPoint(Spline *in, SplineCurrentPoint *iterator, F32 distance)
{
	int new_index;
	F32 new_t;
	F32 out_length;
	F32 remainder;
	splineGetNextPointEx(in, false, zerovec3, iterator->index, eafSize(&in->spline_points)-3, iterator->t, 0,
		distance, &new_index, &new_t, &out_length, &remainder, iterator->position, iterator->up, iterator->tangent);
	iterator->index = new_index;
	iterator->t = new_t;
	return out_length;
}

void splineGetPrevPoint(Spline *in, bool linearize, Vec3 offset, int begin_pt, int end_pt, F32 begin_t, F32 end_t, F32 length, 
						int *new_index, F32 *new_t, 
						F32 *exact_length, F32 *length_remainder, Vec3 exact_point, Vec3 exact_up, Vec3 exact_tangent)
{
	int i;
	F32 current_length = 0;
	bool first_point = true;
	Vec3 last_point, last_up, last_tangent;

	for (i = end_pt; i >= begin_pt; i-=3)
	{
		F32 t0 = 0, t1 = 1;
		F32 t;

		if (i == begin_pt) t0 = begin_t;
		if (i == end_pt) t1 = end_t;

		for (t = t1; t >= t0; t -= 1.f/SPLINE_SEGMENTS)
		{
			Vec3 cur_point, cur_up, cur_tangent;
			F32 distance = 0, last_length;
			splineTransform(in, i, t, offset, cur_point, cur_up, cur_tangent, linearize);
			last_length = current_length;
			if (!first_point)
			{
				distance = distance3(last_point, cur_point);
				current_length += distance;
			}
			if (current_length >= length)
			{
				F32 exact_t = t;
				if (!first_point && distance > 0)
				{
					// Find intersection t
					F32 interp = (length - last_length) / distance;
					exact_t = t - (1.f/SPLINE_SEGMENTS)*(1-interp);

					scaleVec3(last_point, (1-interp), last_point);
					scaleVec3(cur_point, interp, exact_point);
					addVec3(exact_point, last_point, exact_point);

					scaleVec3(last_up, (1-interp), last_up);
					scaleVec3(cur_up, interp, exact_up);
					addVec3(exact_up, last_up, exact_up);

					scaleVec3(last_tangent, (1-interp), last_tangent);
					scaleVec3(cur_tangent, interp, exact_tangent);
					addVec3(exact_tangent, last_tangent, exact_tangent);
					normalVec3(exact_tangent);
				}
				else
				{
					copyVec3(cur_point, exact_point);
					copyVec3(cur_up, exact_up);
					copyVec3(cur_tangent, exact_tangent);
				}

				*exact_length = length;
				*length_remainder = current_length-length;
				*new_index = i;
				*new_t = t;
				return;
			}
			copyVec3(cur_point, last_point);
			copyVec3(cur_up, last_up);
			copyVec3(cur_tangent, last_tangent);
			first_point = false;
		}
	}

	copyVec3(last_point, exact_point);
	copyVec3(last_up, exact_up);
	copyVec3(last_tangent, exact_tangent);
	*exact_length = current_length;
	*length_remainder = 0;
	*new_index = begin_pt;
	*new_t = begin_t;
}

void get_endpoint(Spline *in, F32 coord, int *out_pt, F32 *out_t, bool is_end)
{
	if (coord >= 1)
	{
		*out_pt = (int)coord;
		*out_t = coord - *out_pt;
		*out_pt = (*out_pt-1) * 3;
	}
	else if (coord <= -1)
	{
		*out_pt = (int)-coord;
		*out_t = 1.f - (-coord - *out_pt);
		*out_pt = eafSize(&in->spline_points) - 3 - *out_pt * 3;
	}
	else
	{
		if (is_end)	*out_pt = eafSize(&in->spline_points) - 3;
		else		*out_pt = 0;
		*out_t = 0;
	}

	if (*out_pt < 0)
	{
		*out_pt = 0;
		*out_t = 0;
	}
	if (*out_pt > eafSize(&in->spline_points)-3)
	{
		*out_pt = eafSize(&in->spline_points)-3;
		*out_t = 0;
	}
}

int GCD(int a, int b)
{
	if (b > a) return GCD(b,a);
	if (b == 0) return a;
	return GCD(b, a%b);
}

#define MAX_GEOM_COUNT 20
void find_best_factorization(int idx, int num_lengths, int *geom_lengths, int *geom_counts, int *num_defaults, int *temp_geom_counts, int total_length, int default_length, int *remainder)
{
	int count;
	int length = geom_lengths[idx];
	int new_remainder;
	int new_num_defaults;
	if (length == default_length)
	{
			if (idx < num_lengths-1)
			{
				find_best_factorization(idx+1, num_lengths, geom_lengths, geom_counts, num_defaults, temp_geom_counts, total_length, default_length, remainder);
			}
	}
	else
		for (count = 0; count < MAX_GEOM_COUNT; count++)
		{
			temp_geom_counts[idx] = count;

			new_remainder = total_length - count*length;
			if (new_remainder < 0)
			{
				temp_geom_counts[idx] = 0;
				return;
			}

			new_num_defaults = new_remainder / default_length;
			new_remainder -= new_num_defaults * default_length;

			if (new_remainder < *remainder)
			{
				memcpy(geom_counts, temp_geom_counts, num_lengths*sizeof(int));
				*num_defaults = new_num_defaults;
				*remainder = new_remainder;
				if (new_remainder == 0) return;
			}

			if (idx < num_lengths-1)
			{
				find_best_factorization(idx+1, num_lengths, geom_lengths, geom_counts, num_defaults, temp_geom_counts, total_length, default_length, remainder);
			}
			if (*remainder == 0) return;
		}
	temp_geom_counts[idx] = 0;
}

void splineCalculateNormalizedSegment(Spline *in, Spline *out, CurveGeometryDef **geometries, int max_cps,
                                      bool deform, bool linearize, bool extra_point, F32 repeat_length, F32 stretch_factor,
                                      Vec3 offset, int begin_pt, F32 begin_t, int end_pt, F32 end_t)
{
	F32 total_length;
	int i_total_length;
	int last_index, next_index;
	F32 last_t, next_t, length_remainder;
	Vec3 exact_point, exact_up, exact_tangent, last_exact_point;
	F32 cur_stretch_factor, last_size = 0;

	int total_geoms = 0;
	int start_index = eaiSize(&out->spline_geom);

	int i, j;
	int length_gcd;
	int current_geom;
	int num_lengths;
	int default_length;
	int remainder;
	int num_defaults;
	int *geom_length_indices = NULL;
	int *geom_lengths = NULL;
	int *geom_counts = NULL;
	int *geom_space = NULL;
	int *geom_counter = NULL;


    if (eafSize(&out->spline_points)/3 > max_cps)
        return;
    
	splineGetNextPointEx(in, linearize, offset, begin_pt, end_pt, begin_t, end_t, 0.f, 
						&next_index, &next_t, 
						&total_length, &length_remainder, exact_point, exact_up, exact_tangent);

	splineAppendCP(out, exact_point, exact_up, exact_tangent, 0, 0);


    if (eafSize(&out->spline_points)/3 > max_cps)
        return;
    
	splineGetNextPointEx(in, linearize, offset, begin_pt, end_pt, begin_t, end_t, 1000000.f, 
						&next_index, &next_t, 
						&total_length, &length_remainder, exact_point, exact_up, exact_tangent);

	if (total_length <= 0)
	{
		eaiDestroy(&geom_length_indices);
		eaiDestroy(&geom_lengths);
		eaiDestroy(&geom_counts);
		eaiDestroy(&geom_space);
		eaiDestroy(&geom_counter);
		return;
	}

	total_length *= 10.f;

	if (eaSize(&geometries) == 0)
	{
		num_lengths = 0;
		eaiPush(&geom_lengths, (int)(repeat_length*10));
	}
	else
	{
		eaiSetSize(&geom_length_indices, eaSize(&geometries));
		num_lengths = 0;
		for (i = 0; i < eaSize(&geometries); i++)
		{
			bool found = false;
			for (j = 0; j < eaiSize(&geom_lengths); j++)
				if (geom_lengths[j] == (int)(geometries[i]->geometry_length*10))
				{
					geom_length_indices[i] = j;
					found = true;
					break;
				}
			if (!found)
			{
				geom_length_indices[i] = eaiSize(&geom_lengths);
				eaiPush(&geom_lengths, (int)(geometries[i]->geometry_length*10));
				num_lengths++;
			}
		}

		if (num_lengths <= 0)
		{
			eaiDestroy(&geom_length_indices);
			eaiDestroy(&geom_lengths);
			eaiDestroy(&geom_counts);
			eaiDestroy(&geom_space);
			eaiDestroy(&geom_counter);
			return;
		}
	}

	eaiSetSize(&geom_counts, num_lengths);
	length_gcd = geom_lengths[0];
	default_length = geom_lengths[0];
	for (i = 1; i < num_lengths; i++)
	{
		length_gcd = GCD(length_gcd, geom_lengths[i]);
		if (fabs(repeat_length*10 - geom_lengths[i]) - fabs(repeat_length*10 - default_length))
			default_length = geom_lengths[i];
		geom_counts[i] = 0;
	}
	default_length = MAX(1, default_length);

	i_total_length = ((int)total_length) - ((int)total_length)%MAX(1,length_gcd); 

	if (i_total_length <= 0)
	{
		eaiDestroy(&geom_length_indices);
		eaiDestroy(&geom_lengths);
		eaiDestroy(&geom_counts);
		eaiDestroy(&geom_space);
		eaiDestroy(&geom_counter);
		return;
	}

	// Factor length into geometry lengths
	remainder = total_length;
	num_defaults = 0;
	if (num_lengths > 1)
	{
		int *temp_geom_counts = NULL;
		eaiSetSize(&temp_geom_counts, num_lengths);
		for (i = 0; i < num_lengths; i++) temp_geom_counts[i] = 0;
		find_best_factorization(0, num_lengths, geom_lengths, geom_counts, &num_defaults, temp_geom_counts, i_total_length, default_length, &remainder);
		eaiDestroy(&temp_geom_counts);
	}
	else num_defaults = i_total_length/default_length;
	
	total_geoms = 0;
	for (i = 0; i < num_lengths; i++)
	{
		if (geom_lengths[i] == default_length) geom_counts[i] = num_defaults;
		total_geoms += geom_counts[i];
	}

	assert(geom_lengths);
	assert(geom_counts);

	remainder = total_length;
	for (i = 0; i < eaiSize(&geom_lengths); i++)
	{
		if (geom_counts[i])
			eaiPush(&geom_space, total_geoms / geom_counts[i]);
		else
			eaiPush(&geom_space, total_geoms*10);
		eaiPush(&geom_counter, 0);
		remainder -= geom_counts[i]*geom_lengths[i];
	}

	if (remainder > 0 && total_length-remainder > 0)
	{
		cur_stretch_factor = total_length / (total_length - remainder);
		cur_stretch_factor = CLAMP(stretch_factor, 1.f, 1.f+stretch_factor);
	}
	else cur_stretch_factor = 1.f;

	length_remainder = 0;
	next_index = begin_pt;
	next_t = begin_t;

	current_geom = 0;

	assert(geom_counter);
	assert(geom_space);

	last_index = next_index;
	last_t = next_t;

	total_length *= 0.1f;

	for (i = 0; i < total_geoms; i++)
	{
		// Select a length
		int selected_geom = -1;
		int selected_geom_index = -1;
		int gap_type = -1;
		F32 next_gap_length = 0;
		while (selected_geom == -1)
		{
			if (geom_counter[current_geom] >= geom_space[current_geom] &&
				geom_counts[current_geom] > 0)
			{
				selected_geom = current_geom;
				geom_counter[selected_geom] = 0;
				geom_counts[current_geom]--;
			}
			else
				geom_counter[current_geom]++;

			current_geom++;
			if (current_geom == eaiSize(&geom_space)) current_geom = 0;
		}
		splineGetNextPointEx(in, linearize, offset, next_index, end_pt, next_t, end_t,
							geom_lengths[selected_geom]*0.1f*cur_stretch_factor-length_remainder,
							&next_index, &next_t, 
							&total_length, &length_remainder, exact_point, exact_up, exact_tangent);

		last_size = geom_lengths[selected_geom]*0.1f*cur_stretch_factor;

		if (total_length == 0)
			break;

		/*gap_type = spline_find_gap(gaps, last_index, last_t, next_index, next_t, &next_gap_length);
		if (gap_type == CURVE_GAP_REPLACE)
		{
			out->spline_geom[i+start_index] = -1;
		}
		else
		{*/
			for (j = 0; j < eaSize(&geometries); j++)
				if ((int)(geometries[j]->geometry_length*10) == geom_lengths[selected_geom])
				{
					selected_geom_index = j;
					break;
				}
			out->spline_geom[i+start_index] = selected_geom_index;
		//}

		/*if (gap_type == CURVE_GAP_PREFIX)
		{
			F32 temp;
			splineAppendCP(out, exact_point, exact_up, exact_tangent, -1);
			splineGetNextPointEx(in, linearize, offset, next_index, end_pt, next_t, end_t, next_gap_length,
								&next_index, &next_t, 
								&temp, &temp, exact_point, exact_up, exact_tangent);
			start_index++; // A little hacky, but otherwise the geometry indices will be wrong

            if (eafSize(&out->spline_points)/3 > max_cps)
                break;
		}*/

		last_index = next_index;
		last_t = next_t;

		splineAppendCP(out, exact_point, exact_up, exact_tangent, -1, 0);

		copyVec3(exact_point, last_exact_point);

        if (eafSize(&out->spline_points)/3 > max_cps)
            break;
	}

    if (eafSize(&out->spline_points)/3 < max_cps &&
        next_index < end_pt || next_t < end_t)
    {
        splineGetPrevPoint(in, linearize, offset, begin_pt, end_pt, begin_t, end_t, 0.f, 
                           &next_index, &next_t, 
                           &total_length, &length_remainder, exact_point, exact_up, exact_tangent);

        if (deform && 
            distance3(exact_point, last_exact_point) < last_size * stretch_factor)
        {
            int idx = eafSize(&out->spline_points)-3;
            copyVec3(exact_point, &out->spline_points[idx]);
            copyVec3(exact_tangent, &out->spline_deltas[idx]);
            copyVec3(exact_up, &out->spline_up[idx]);
        }
        else
			splineAppendCP(out, exact_point, exact_up, exact_tangent, extra_point ? 0 : -1, 0);
    }

	eaiDestroy(&geom_length_indices);
	eaiDestroy(&geom_lengths);
	eaiDestroy(&geom_counts);
	eaiDestroy(&geom_space);
	eaiDestroy(&geom_counter);

	splineValidate(out);
}

void splineCalculateRandomizedSegment(Spline *in, Spline *out, CurveGeometryDef **geometries, int max_cps,
                                      bool deform, bool linearize, F32 repeat_length, F32 stretch_factor,
                                      U32 seed, Vec3 offset, int begin_pt, F32 begin_t, int end_pt, F32 end_t)
{
	F32 total_length;
	int *def_array;

	int cur_point;
	int last_index, next_index;
	F32 last_t, next_t;
	Vec3 exact_point, exact_up, exact_tangent, last_exact_point;
	F32 length, length_remainder = 0.f;
	F32 min_geo_length, last_size;
	int geo, num_defs;

	splineGetNextPointEx(in, linearize, offset, begin_pt, end_pt, begin_t, end_t, 1000000.f, 
						&next_index, &next_t, 
						&total_length, &length_remainder, exact_point, exact_up, exact_tangent);

	if (total_length < 0)
		return;
	if (!geometries || eaSize(&geometries) == 0)
		return;

	def_array = ScratchAlloc(eaSize(&geometries) * sizeof(int));
	srand(seed);

	assert(geometries);
	min_geo_length = geometries[0]->geometry_length*10;
	for (geo = 1; geo < eaSize(&geometries); geo++)
		min_geo_length = MIN(geometries[geo]->geometry_length*10, min_geo_length);

	if (total_length < min_geo_length)
	{
		ScratchFree(def_array);
		return;
	}

	cur_point = eaiSize(&out->spline_geom);

	// BEGIN POINT
	splineGetNextPointEx(in, linearize, offset, begin_pt, end_pt, begin_t, end_t, 0, 
						&next_index, &next_t, 
						&length, &length_remainder, exact_point, exact_up, exact_tangent);

	splineAppendCP(out, exact_point, exact_up, exact_tangent, 0, 0);


    if (eafSize(&out->spline_points)/3 > max_cps)
        return;
    
	last_index = next_index;
	last_t = next_t;

	while (total_length > min_geo_length)
	{
		F32 next_gap_length = 0;
		num_defs = 0;
		geo = -1;

		for (geo = 0; geo < eaSize(&geometries); geo++)
		{
			if (total_length > geometries[geo]->geometry_length*10)
				def_array[num_defs++] = geo;
		}
		if (num_defs == 0)
			break;

		geo = rand() % num_defs;

		splineGetNextPointEx(in, linearize, offset, next_index, end_pt, next_t, end_t, geometries[def_array[geo]]->geometry_length - length_remainder, 
							&next_index, &next_t, 
							&length, &length_remainder, exact_point, exact_up, exact_tangent);

		if (length > 0)
		{
			//int found_gap = spline_find_gap(gaps, last_index, last_t, next_index, next_t, &next_gap_length);
			//out->spline_geom[cur_point++] = (found_gap == CURVE_GAP_REPLACE) ? -1 : def_array[geo];
			out->spline_geom[cur_point++] = def_array[geo];
			splineAppendCP(out, exact_point, exact_up, exact_tangent, -1, 0);
			/*if (total_length-length > min_geo_length)
			{
				if (found_gap == CURVE_GAP_PREFIX)
				{
					F32 temp;
					splineGetNextPointEx(in, linearize, offset, next_index, end_pt, next_t, end_t, next_gap_length,
										&next_index, &next_t, 
										&temp, &temp, exact_point, exact_up, exact_tangent);
					splineAppendCP(out, exact_point, exact_up, exact_tangent, -1, 0);
					out->spline_geom[cur_point++] = -1;

                    if (eafSize(&out->spline_points)/3 > max_cps)
                        break;
				}
			}*/
		}
		else
			break;

		copyVec3(exact_point, last_exact_point);
		last_size = geometries[def_array[geo]]->geometry_length*10;

		last_index = next_index;
		last_t = next_t;

		total_length -= length;

		if (eafSize(&out->spline_points)/3 > max_cps)
			break;
	}

	// End point

    if (eafSize(&out->spline_points)/3 < max_cps)
	{    
        splineGetPrevPoint(in, linearize, offset, begin_pt, end_pt, begin_t, end_t, 0.f, 
                           &next_index, &next_t, 
                           &total_length, &length_remainder, exact_point, exact_up, exact_tangent);

        if (deform && 
            eafSize(&out->spline_points) > 0 &&
            distance3(exact_point, last_exact_point) < last_size*stretch_factor)
        {
            int idx = eafSize(&out->spline_points)-3;
            copyVec3(exact_point, &out->spline_points[idx]);
            copyVec3(exact_tangent, &out->spline_deltas[idx]);
            copyVec3(exact_up, &out->spline_up[idx]);
        }
        else
            splineAppendCP(out, exact_point, exact_up, exact_tangent, -1, 0);
    }

	ScratchFree(def_array);

	splineValidate(out);
}

void splineCalculateStretchedSegment(Spline *in, Spline *out, CurveGeometryDef **geometries, int max_cps,
                                     bool deform, bool linearize, F32 repeat_length, F32 stretch_factor,
                                     Vec3 offset, int begin_pt, F32 begin_t, int end_pt, F32 end_t)
{
	int i;
	//bool found_gap;
	Vec3 exact_point, exact_up, exact_tangent;

	for (i = begin_pt; i <= end_pt; i+=3)
	{
		F32 in_t = 0;
		if (i == begin_pt) in_t = begin_t;

		//found_gap = (spline_find_gap(gaps, i, in_t, i, in_t, NULL) == CURVE_GAP_REPLACE); // Don't support CURVE_GAP_PREFIX yet

		splineTransform(in, i, in_t, offset, exact_point, exact_up, exact_tangent, linearize);
		splineAppendCP(out, exact_point, exact_up, exact_tangent, 0, 0);

        if (eafSize(&out->spline_points)/3 > max_cps)
            break;
	}

    if (eafSize(&out->spline_points)/3 < max_cps &&
        end_t != 0)
	{
		//found_gap = (spline_find_gap(gaps, i-3, end_t, i-3, end_t, NULL) == CURVE_GAP_REPLACE);
		splineTransform(in, i-3, end_t, offset, exact_point, exact_up, exact_tangent, linearize);
		splineAppendCP(out, exact_point, exact_up, exact_tangent, 0, 0);
	}

	splineValidate(out);
}

typedef struct SplineInternalSegment
{
	int begin_pt;
	F32 begin_t;
	int end_pt;
	F32 end_t;
} SplineInternalSegment;

void splineReverse(Spline *spline)
{
	int i;
	splineValidate(spline);
	for (i = 0; i < (eafSize(&spline->spline_points)-3)/2; i+=3)
	{
		int j;
		Vec3 temp;
		j = eafSize(&spline->spline_points)-3-i;

		copyVec3(&spline->spline_points[j], temp);
		copyVec3(&spline->spline_points[i], &spline->spline_points[j]);
		copyVec3(temp, &spline->spline_points[i]);

		copyVec3(&spline->spline_up[j], temp);
		copyVec3(&spline->spline_up[i], &spline->spline_up[j]);
		copyVec3(temp, &spline->spline_up[i]);

		copyVec3(&spline->spline_deltas[j], temp);
		copyVec3(&spline->spline_deltas[i], &spline->spline_deltas[j]);
		copyVec3(temp, &spline->spline_deltas[i]);
	}
	// Swap geometry types, omitting last value
	for (i = 0; i < (ea32Size(&spline->spline_geom)-1)/2; i++)
	{
		int j;
		S32 tempu;
		j = ea32Size(&spline->spline_geom)-2-i;

		tempu = spline->spline_geom[j];
		spline->spline_geom[j] = spline->spline_geom[i];
		spline->spline_geom[i] = tempu;
	}
	for (i = 0; i < eafSize(&spline->spline_points); i+=3)
		scaleVec3(&spline->spline_deltas[i], -1, &spline->spline_deltas[i]);
	splineValidate(spline);
}

void splineRotate(Spline *spline, Vec3 rotate_vec)
{
	Mat3 local_mat;
	Mat3 rotate_matrix;
	Vec3 rotate_vec_rad;
	int i;

	scaleVec3(rotate_vec, 3.1415f/180.f, rotate_vec_rad);
	createMat3YPR(rotate_matrix, rotate_vec_rad);
	for (i = 0; i < eafSize(&spline->spline_points); i+=3)
	{
		Vec3 temp, temp2;
		Mat3 inv_mat;
		copyVec3(&spline->spline_up[i], local_mat[1]);
		copyVec3(&spline->spline_deltas[i], local_mat[2]);
		crossVec3(local_mat[1], local_mat[2], local_mat[0]);
		copyMat3(local_mat, inv_mat);
		transposeMat3(inv_mat);
		mulVecMat3(&spline->spline_deltas[i], inv_mat, temp);
		mulVecMat3(temp, rotate_matrix, temp2);
		mulVecMat3(temp2, local_mat, temp);
		copyVec3(temp, &spline->spline_deltas[i]);

		mulVecMat3(&spline->spline_up[i], inv_mat, temp);
		mulVecMat3(temp, rotate_matrix, temp2);
		mulVecMat3(temp2, local_mat, temp);
		copyVec3(temp, &spline->spline_up[i]);
	}
	splineValidate(spline);
}

void splineResetUp(Spline *spline)
{
	int i;

	for (i = 0; i < eafSize(&spline->spline_points); i+=3)
	{
		setVec3(&spline->spline_up[i], 0, 1, 0);
	}
	splineValidate(spline);
}

void splineLinearize(Spline *spline)
{
	int i;

	for (i = 0; i < eafSize(&spline->spline_points)-3; i+=3)
	{
		subVec3(&spline->spline_points[i+3], &spline->spline_points[i], &spline->spline_deltas[i]);
		normalVec3(&spline->spline_deltas[i]);
	}
	splineValidate(spline);
}

void splineRotateNEW(Spline *spline, const Mat3 rotate_matrix)
{
	Mat3 local_mat;
	int i;

	for (i = 0; i < eafSize(&spline->spline_points); i+=3)
	{
		Vec3 temp, temp2;
		Mat3 inv_mat;
		copyVec3(&spline->spline_up[i], local_mat[1]);
		copyVec3(&spline->spline_deltas[i], local_mat[2]);
		crossVec3(local_mat[1], local_mat[2], local_mat[0]);
		copyMat3(local_mat, inv_mat);
		transposeMat3(inv_mat);
		mulVecMat3(&spline->spline_deltas[i], inv_mat, temp);
		mulVecMat3(temp, rotate_matrix, temp2);
		mulVecMat3(temp2, local_mat, temp);
		copyVec3(temp, &spline->spline_deltas[i]);

		mulVecMat3(&spline->spline_up[i], inv_mat, temp);
		mulVecMat3(temp, rotate_matrix, temp2);
		mulVecMat3(temp2, local_mat, temp);
		copyVec3(temp, &spline->spline_up[i]);
	}
	splineValidate(spline);
}

void splineTransformMatrix(Spline *spline, const Mat4 matrix)
{
	int i;
	for (i = 0; i < eafSize(&spline->spline_points); i += 3)
	{
		Vec3 point;
		copyVec3(&spline->spline_points[i], point);
		mulVecMat4(point, matrix, &spline->spline_points[i]);
		copyVec3(&spline->spline_deltas[i], point);
		mulVecMat3(point, matrix, &spline->spline_deltas[i]);
		copyVec3(&spline->spline_up[i], point);
		mulVecMat3(point, matrix, &spline->spline_up[i]);
	}
}

F32 curveGetGeometryLength(GroupDef *group_def, bool scaled)
{
    F32 length = 10.f;
    if (group_def->property_structs.child_curve && group_def->property_structs.child_curve->geo_length) {
        length = group_def->property_structs.child_curve->geo_length;
	} else {
        length = group_def->bounds.max[2] - group_def->bounds.min[2];
        if (length < 0.1f)
            length = 10.f;
    }
	if (scaled && group_def->model)
		length *= group_def->model_scale[0];
    return ((int)(length*10)) * 0.1f;
}

void curveCalculateChild(Spline *in, Spline *out, WorldChildCurve *child, U32 seed, GroupDef *group_def, const Mat4 group_matrix)
{
    static const Vec3 zero_offset = { 0, 0, 0 }; 
	int i;//, j, k;
	int begin_pt, end_pt;
	F32 begin_t, end_t;

	F32 length_remainder, total_length;
	Vec3 exact_point, exact_up, exact_tangent;
	Vec3 exact_point_2, exact_up_2, exact_tangent_2;

	//SplineInternalSegment *segment = ScratchAlloc(1 + eaSize(&gaps) * sizeof(SplineInternalSegment));
	SplineInternalSegment segment[1];
	int num_segments = 1;
    CurveGeometryDef **geometries = NULL;
	int max_cps = child ? MAX(1, child->max_cps) : 250;
    Vec3 attach_offset;

	splineDestroy(out);

	if (eafSize(&in->spline_points) < 3 || (child && child->repeat_length <= 0))
	{
		//ScratchFree(segment);
		return;
	}

    // TomY TODO - fix this in the actual eval code, instead of here
    setVec3(attach_offset, -group_matrix[3][0], group_matrix[3][1], group_matrix[3][2]); 

	get_endpoint(in, child ? child->begin_offset : 0, &begin_pt, &begin_t, false);
	get_endpoint(in, child ? child->end_offset : 0, &end_pt, &end_t, true);
	if (end_pt < begin_pt) end_pt = begin_pt;
	if (end_pt == begin_pt && end_t < begin_t) end_t = begin_t;

	// Begin & End Pad
	splineGetNextPointEx(in, child ? child->linearize : false,
                       attach_offset,
                       begin_pt, end_pt, begin_t, end_t, child ? child->begin_pad : 0.f, 
						&begin_pt, &begin_t, 
						&total_length, &length_remainder, exact_point, exact_up, exact_tangent);

	splineGetPrevPoint(in, child ? child->linearize : false,
                       attach_offset,
                       begin_pt, end_pt, begin_t, end_t, child ? child->end_pad : 0.f, 
						&end_pt, &end_t, 
						&total_length, &length_remainder, exact_point_2, exact_up_2, exact_tangent_2);

	segment[0].begin_pt = begin_pt;
	segment[0].begin_t = begin_t;
	segment[0].end_pt = end_pt;
	segment[0].end_t = end_t;

	/*if (!child || !child->avoid_gaps)
	{
		for (i = 0; i < eaSize(&gaps); i++)
		{
			CurveGap *gap = gaps[i];
			int gap_pt = gap->offset;
			F32 gap_t = gap->offset-gap_pt;
			splineGetPrevPoint(in, child ? child->linearize : false,
                               attach_offset,
                               0, (gap_pt-1)*3, 0, gap_t, gap->size/2, 
								&gap->begin_pt, &gap->begin_t, 
								&total_length, &length_remainder, exact_point, exact_up, exact_tangent);

			splineGetNextPointEx(in, child ? child->linearize : false,
                               attach_offset,
                               (gap_pt-1)*3, eafSize(&in->spline_points)-3, gap_t, 0, gap->size/2,
								&gap->end_pt, &gap->end_t, 
								&total_length, &length_remainder, exact_point, exact_up, exact_tangent);
			if (gap->type == CURVE_GAP_EXACT)
			{

				if (gap->end_pt < gap->begin_pt || (gap->end_pt == gap->begin_pt && gap->end_t < gap->begin_t)) continue;

				for (j = 0; j < num_segments; j++)
				{
					bool trim_begin, trim_end;
					if (segment[j].end_pt < gap->begin_pt || (segment[j].end_pt == gap->begin_pt && segment[j].end_t <= gap->begin_t))
						continue;
					if (segment[j].begin_pt > gap->end_pt || (segment[j].begin_pt == gap->end_pt && segment[j].begin_t >= gap->end_t))
						continue;

					trim_begin = (segment[j].begin_pt > gap->begin_pt || (segment[j].begin_pt == gap->begin_pt && segment[j].begin_t >= gap->begin_t));
					trim_end = (segment[j].end_pt < gap->end_pt || (segment[j].end_pt == gap->end_pt && segment[j].end_t <= gap->end_t));

					if (trim_end && trim_begin)
					{
						// Delete this segment
						if (j < num_segments-1) segment[j] = segment[num_segments-1];
						num_segments--;
						j--;
						continue;
					}
					if (trim_end)
					{
						// Trim end segment only
						segment[j].end_pt = gap->begin_pt;
						segment[j].end_t = gap->begin_t;
						continue;
					}
					if (trim_begin)
					{
						// Trim begin segment only
						segment[j].begin_pt = gap->end_pt;
						segment[j].begin_t = gap->end_t;
						continue;
					}
					// Split into two segments (keep them sorted)
					for (k = num_segments-1; k >= j+1; k--)
						segment[k+1] = segment[k];
					segment[j+1].begin_pt = gap->end_pt;
					segment[j+1].begin_t = gap->end_t;
					segment[j+1].end_pt = segment[j].end_pt;
					segment[j+1].end_t = segment[j].end_t;
					segment[j].end_pt = gap->begin_pt;
					segment[j].end_t = gap->begin_t;
					num_segments++;
				}
			}
		}
	}*/

	if (group_def)
	{
		if (child && 
			(child->child_type == CURVE_CHILD_RANDOM ||
			child->child_type == CURVE_CHILD_OPTIMIZE))
		{
			GroupChild **children = groupGetChildren(group_def);
			for (i = 0; i < eaSize(&children); i++)
			{
				GroupDef *def = groupChildGetDef(group_def, children[i], false);
				if (def)
				{
					CurveGeometryDef *geom_def = calloc(1, sizeof(CurveGeometryDef));
					geom_def->geometry_length = curveGetGeometryLength(def, true);
					eaPush(&geometries, geom_def);
				}
			}
		}
		else
		{
			CurveGeometryDef *geom_def = calloc(1, sizeof(CurveGeometryDef));
			geom_def->geometry_length = curveGetGeometryLength(group_def, true);
			eaPush(&geometries, geom_def);
		}
	}

	for (i = 0; i < num_segments; i++)
	{
		if (group_def && child && child->child_type == CURVE_CHILD_RANDOM)
		{
			splineCalculateRandomizedSegment(in, out, geometries, max_cps,
                                             child ? child->deform : false,
                                             child ? child->linearize : false,
                                             child ? child->repeat_length : 100.f,
                                             child ? child->stretch_factor : 1.f,
                                             seed,
                                             attach_offset,
                                             segment[i].begin_pt, segment[i].begin_t, segment[i].end_pt, segment[i].end_t);
		}
		else if (group_def && (!child || child->normalize))
		{
			splineCalculateNormalizedSegment(in, out, geometries, max_cps,
                                             child ? child->deform : false,
                                             child ? child->linearize : false,
                                             child ? child->extra_point : false,
                                             child ? child->repeat_length : 100.f,
                                             child ? child->stretch_factor : 1.f,
                                             attach_offset,
                                             segment[i].begin_pt, segment[i].begin_t, segment[i].end_pt, segment[i].end_t);
		}
		else
		{
			splineCalculateStretchedSegment(in, out, geometries, max_cps,
                                             child ? child->deform : false,
                                             child ? child->linearize : false,
                                             child ? child->repeat_length : 100.f,
                                             child ? child->stretch_factor : 1.f,
                                             attach_offset,
                                             segment[i].begin_pt, segment[i].begin_t, segment[i].end_pt, segment[i].end_t);
		}
    }
    
    eaDestroyEx(&geometries, NULL);

	//ScratchFree(segment);

    if (child)
    {
        if (child->reverse_curve)
            splineReverse(out);

        if (child->reset_up)
            splineResetUp(out);

        if (child->linearize)
            splineLinearize(out);
    }
    splineRotateNEW(out, group_matrix);
}

/*void curveCreateOcclusionVolume(Curve *curve, GroupDef *def)
{
	int i;
	int num_cps;
	F32 offset;
	F32 *vert_ptr;
	U32 *tri_ptr;
	Model *model;
	ModelLOD *model_lod;
	ModelLODData *model_data;
	bool begin_cap = curve->occlusion_bits & 1;
	bool end_cap = curve->occlusion_bits & 2;
	int num_sides = ((curve->occlusion_bits & 4) ? 1 : 0) +
		((curve->occlusion_bits & 8) ? 1 : 0) + 
		((curve->occlusion_bits & 16) ? 1 : 0) + 
		((curve->occlusion_bits & 32) ? 1 : 0);

	num_cps = eafSize(&curve->spline.spline_points)/3;
	if (num_cps < 2)
		return;

	offset = curve->occlusion_radius * 0.70711f;

	model = tempModelAlloc("Curve Occlusion", &default_material, 1, WL_FOR_WORLD);
	model_lod = model->model_lods[0];
	model_data = model_lod->data;

	// Create model info
	model_data->tri_count = (begin_cap ? 2 : 0) + (end_cap ? 2 : 0) + (num_cps-1)*8*num_sides;
	model_data->vert_count = 16*(num_cps-1) + 4;
	model_data->tex_idx[0].count = model_data->tri_count;

	vert_ptr = (F32*)model_lod->unpack.verts = calloc(model_data->vert_count, sizeof(model_lod->unpack.verts[0]));
	tri_ptr = model_lod->unpack.tris = calloc(model_data->tri_count*3, sizeof(model_lod->unpack.tris[0]));

	for (i = 0; i < num_cps-1; i++)
	{
		F32 t = (i == 0) ? 0.f : 0.25f;
		for (; t <= 1.f; t += 0.25f)
		{
			Vec3 in, tangent, up;
			setVec3(in, -offset, -offset, 0.f);
			splineTransform(&curve->spline, i*3, t, in, vert_ptr, up, tangent, false);
			vert_ptr+=3;
			setVec3(in, -offset, offset, 0.f);
			splineTransform(&curve->spline, i*3, t, in, vert_ptr, up, tangent, false);
			vert_ptr+=3;
			setVec3(in, offset, offset, 0.f);
			splineTransform(&curve->spline, i*3, t, in, vert_ptr, up, tangent, false);
			vert_ptr+=3;
			setVec3(in, offset, -offset, 0.f);
			splineTransform(&curve->spline, i*3, t, in, vert_ptr, up, tangent, false);
			vert_ptr+=3;
		}
	}
	assert(vert_ptr == (F32*)&model_lod->unpack.verts[model_data->vert_count]);

	// start cap
	if (begin_cap)
	{
#define SET_TRI(i0, i1, i2) *tri_ptr++=(i0); *tri_ptr++=(i1); *tri_ptr++=(i2);
		SET_TRI(0, 1, 2);
		SET_TRI(0, 2, 3);
	}

	// end cap
	if (end_cap)
	{
		i = model_data->vert_count;
		SET_TRI(i-1, i-2, i-3);
		SET_TRI(i-1, i-3, i-4);
	}

	for (i = 0; i < (num_cps-1)*4; i++)
	{
		if (curve->occlusion_bits & 16)
		{
			SET_TRI(i*4, i*4+5, i*4+1);
			SET_TRI(i*4, i*4+4, i*4+5);
		}

		if (curve->occlusion_bits & 4)
		{
			SET_TRI(i*4+1, i*4+5, i*4+6);
			SET_TRI(i*4+1, i*4+6, i*4+2);
		}

		if (curve->occlusion_bits & 8)
		{
			SET_TRI(i*4+2, i*4+6, i*4+7);
			SET_TRI(i*4+2, i*4+7, i*4+3);
		}

		if (curve->occlusion_bits & 32)
		{
			SET_TRI(i*4+3, i*4+4, i*4+0);
			SET_TRI(i*4+3, i*4+7, i*4+4);
		}
	}
	assert(tri_ptr == &model_lod->unpack.tris[model_data->tri_count*3]);

	modelLODInitFromData(model_lod);
	model_lod->loadstate = GEO_LOADED;

	assert(!def->model);
	def->model = model;
	def->bounds.property_flags |= GRP_OCCLUSION_ONLY;

	groupDefModify(def, UPDATE_GROUP_PROPERTIES);
}*/

bool splinePointsAreCollinear(Spline *spline, int idx)
{
	Vec3 dir;
	subVec3(&spline->spline_points[idx+3], &spline->spline_points[idx], dir);
	normalVec3(dir);

	if (dotVec3(dir, &spline->spline_deltas[idx]) > 0.9999f &&
			dotVec3(dir, &spline->spline_deltas[idx+3]) > 0.9999f)
		return true;
	return false;
}

bool splinePointsAreIdentical(Spline *spline, int idx)
{
	return spline && (idx+3 < eafSize(&spline->spline_points)) &&
		distance3(&spline->spline_points[idx+3], &spline->spline_points[idx]) < 0.01f;
}

int splineCollide(Vec3 start, Vec3 end, Mat4 parent_mat, Spline *spline)
{
	int i;
	Spline *xformed_spline = StructCreate(parse_Spline);
	StructCopyAll(parse_Spline, spline, xformed_spline);
	splineTransformMatrix(xformed_spline, parent_mat);
	// Draw spline_points
	for (i = 0; i < eafSize(&xformed_spline->spline_points); i+=3)
	{
		Vec3 min, max, intersect;

		setVec3(max, 3, 2, 3);
		setVec3(min, -3, -2, -3);

		addVec3(&xformed_spline->spline_points[i], max, max);
		addVec3(&xformed_spline->spline_points[i], min, min);

		if (lineBoxCollision( start, end, min, max, intersect ))
		{
			StructDestroy(parse_Spline, xformed_spline);
			return i;
		}
	}
	StructDestroy(parse_Spline, xformed_spline);
	return -1;
}

#define MIN4(a,b,c,d) MIN((a), MIN((b), MIN((c), (d))))
#define MAX4(a,b,c,d) MAX((a), MAX((b), MAX((c), (d))))

bool splineCollideFull(Vec3 start, Vec3 end, Mat4 parent_mat, Spline *spline, F32 *collide_offset, Vec3 collide_pos, F32 tolerance)
{
	Spline *xformed_spline;
	Spline spline2 = { 0 };
	F32 offset2;
	bool ret;
	Vec3 dir, up, collide_dir1, collide_dir2;

	setVec3(up, 0, 1, 0);
	subVec3(end, start, dir);
	normalVec3(dir);
	splineAppendCP(&spline2, start, up, dir, 0, 0);
	splineAppendCP(&spline2, end, up, dir, 0, 0);

	xformed_spline = StructCreate(parse_Spline);
	StructCopyAll(parse_Spline, spline, xformed_spline);
	splineTransformMatrix(xformed_spline, parent_mat);

	ret = splineCheckCollision(xformed_spline, &spline2, 0, 0, collide_offset, &offset2, collide_pos, collide_dir1, collide_dir2, tolerance);
	StructDestroy(parse_Spline, xformed_spline);

	return ret;
}

typedef struct spline_coll_segment
{
	F32 t0, t1;
	Vec3 points[5];
	F32 radius;
	F32 distance;
} spline_coll_segment;

bool check_spline_collide(spline_coll_segment *segment1, spline_coll_segment *segment2)
{
	Vec3 delta1, delta2;
	Vec3 end_pt;
	F32 coll_radius;
	subVec3(segment1->points[4], segment1->points[0], delta1);
	subVec3(segment2->points[4], segment2->points[0], delta2);
	subVec3(delta2, delta1, delta2);
	addVec3(delta2, segment2->points[0], end_pt);
	coll_radius = segment1->radius + segment2->radius;
	return sphereLineCollision( coll_radius, segment1->points[0], segment2->points[0], end_pt);
}

void segment_compute_radius(spline_coll_segment *coll_segment)
{
	int i;
	F32 dp, radius_sq, max_radius_sq;
	Vec3 temp, dir, temp2;
	subVec3(coll_segment->points[4], coll_segment->points[0], dir);
	normalVec3(dir);
	max_radius_sq = 0.f;
	for (i = 1; i < 4; i++)
	{
		subVec3(coll_segment->points[i], coll_segment->points[0], temp);
		dp = dotVec3(temp, dir);
		scaleVec3(dir, dp, temp2);
		subVec3(temp, temp2, temp);
		radius_sq = SQR(temp[0]) + SQR(temp[1]) + SQR(temp[2]);
		if (radius_sq > max_radius_sq) max_radius_sq = radius_sq;
	}
	coll_segment->radius = sqrtf(max_radius_sq);
}

F32 spline_check_segments(Vec3 *control_points_1, spline_coll_segment *segments_1, int *num_segments_1, spline_coll_segment *segments_2, int num_segments_2)
{
	int i, j, out;
	F32 max_size;
	spline_coll_segment temp_array[16];

	out = 0;

	if (segments_1->t1 - segments_1->t0 < 0.0000001f)
		return 0;

	for (i = 0; i < *num_segments_1 && out < 16; i++)
	{
		Vec3 dir1;
		F32 length1;
		subVec3(segments_1[i].points[4], segments_1[i].points[0], dir1);
		length1 = normalVec3(dir1);
		for (j = 0; j < num_segments_2; j++)
			if (check_spline_collide(&segments_1[i], &segments_2[j]))
			{
				Vec3 dir2;
				F32 length2;
				Vec3 isect1, isect2;
				subVec3(segments_2[j].points[4], segments_2[j].points[0], dir2);
				length2 = normalVec3(dir2);
				temp_array[out] = segments_1[i];
				temp_array[out++].distance = LineLineDistSquared(	segments_1[i].points[0], dir1, length1, isect1,
																	segments_2[j].points[0], dir2, length2, isect2);
				break;
			}
	}
	if (out > 4)
	{
		int min_j;
		F32 min_distance;
		for (i = 0; i < 4; i++)
		{
			min_distance = temp_array[i].distance;
			min_j = i;
			for (j = i+1; j < out; j++)
				if (temp_array[j].distance < min_distance)
				{
					min_distance = temp_array[j].distance;
					min_j = j;
				}
			if (min_j != i)
			{
				spline_coll_segment temp;
				temp = temp_array[i];
				temp_array[i] = temp_array[min_j];
				temp_array[min_j] = temp;
			}
		}
		out = 4;
	}
	*num_segments_1 = out;
	out = 0;
	for (i = 0; i < *num_segments_1; i++)
	{
		segments_1[out] = temp_array[i];
		segments_1[out+1] = temp_array[i];
		segments_1[out].t1 = (segments_1[out].t0 + segments_1[out].t1) * 0.5f;
		segments_1[out+1].t0 = segments_1[out].t1;
		copyVec3(segments_1[out].points[2], segments_1[out].points[4]);
		copyVec3(segments_1[out].points[1], segments_1[out].points[2]);
		copyVec3(segments_1[out+1].points[2], segments_1[out+1].points[0]);
		copyVec3(segments_1[out+1].points[3], segments_1[out+1].points[2]);
		out += 2;
	}

	max_size = 0;
	for (i = 0; i < out; i++)
	{
		F32 size;
		bezierGetPoint3D(control_points_1, 0.25f * segments_1[i].t1 + 0.75f * segments_1[i].t0, segments_1[i].points[1]);
		bezierGetPoint3D(control_points_1, 0.75f * segments_1[i].t1 + 0.25f * segments_1[i].t0, segments_1[i].points[3]);
		segment_compute_radius(&segments_1[i]);
		size = distance3(segments_1[i].points[0], segments_1[i].points[4]);
		if (size + segments_1[i].radius > max_size) max_size = size + segments_1[i].radius;
	}
	*num_segments_1 = out;
	return max_size;
}

void splineGetBoundingBox(Vec3 control_points[4], Vec3 min, Vec3 max, F32 tol)
{
	setVec3(min, MIN4(control_points[0][0], control_points[1][0], control_points[2][0], control_points[3][0]) - tol,
		MIN4(control_points[0][1], control_points[1][1], control_points[2][1], control_points[3][1]) - tol,
		MIN4(control_points[0][2], control_points[1][2], control_points[2][2], control_points[3][2]) - tol);
	setVec3(max, MAX4(control_points[0][0], control_points[1][0], control_points[2][0], control_points[3][0]) + tol,
		MAX4(control_points[0][1], control_points[1][1], control_points[2][1], control_points[3][1]) + tol,
		MAX4(control_points[0][2], control_points[1][2], control_points[2][2], control_points[3][2]) + tol);
}

bool splineCheckCollision(Spline *spline1, Spline *spline2,
						  F32 start_offset1, F32 start_offset2,
						  F32 *offset1, F32 *offset2, 
						  Vec3 collide_pos, Vec3 collide_dir1, Vec3 collide_dir2, F32 tol)
{
	int idx1, idx2;
	bool found = false;
	for (idx1 = start_offset1; idx1 < eafSize(&spline1->spline_points)-3; idx1 += 3)
	{
		for (idx2 = start_offset2; idx2 < eafSize(&spline2->spline_points)-3; idx2 += 3)
		{
			Vec3 control_points_1[4], control_points_2[4];
			Vec3 min1, max1, min2, max2;

			splineGetControlPoints(spline1, idx1, control_points_1);

			splineGetBoundingBox(control_points_1, min1, max1, tol/2);

			splineGetControlPoints(spline2, idx2, control_points_2);

			splineGetBoundingBox(control_points_2, min2, max2, tol/2);

			if (boxBoxCollision(min1, max1, min2, max2))
			{
				int i, j;
				F32 sizes[2];
				F32 min_distance = tol*100;
				spline_coll_segment segments_1[16], segments_2[16];
				int num_segments_1, num_segments_2;

				segments_1[0].t0 = 0;
				segments_1[0].t1 = 1;
				copyVec3(control_points_1[0], segments_1[0].points[0]);
				copyVec3(control_points_1[3], segments_1[0].points[4]);
				bezierGetPoint3D(control_points_1, 0.25f, segments_1[0].points[1]);
				bezierGetPoint3D(control_points_1, 0.5f, segments_1[0].points[2]);
				bezierGetPoint3D(control_points_1, 0.75f, segments_1[0].points[3]);
				segment_compute_radius(&segments_1[0]);

				segments_2[0].t0 = 0;
				segments_2[0].t1 = 1;
				copyVec3(control_points_2[0], segments_2[0].points[0]);
				copyVec3(control_points_2[3], segments_2[0].points[4]);
				bezierGetPoint3D(control_points_2, 0.25f, segments_2[0].points[1]);
				bezierGetPoint3D(control_points_2, 0.5f, segments_2[0].points[2]);
				bezierGetPoint3D(control_points_2, 0.75f, segments_2[0].points[3]);
				segment_compute_radius(&segments_2[0]);

				num_segments_1 = num_segments_2 = 1;

				do
				{
					sizes[0] = spline_check_segments(control_points_1, segments_1, &num_segments_1, segments_2, num_segments_2);
					sizes[1] = spline_check_segments(control_points_2, segments_2, &num_segments_2, segments_1, num_segments_1);
				} while (num_segments_1 > 0 && num_segments_2 > 0 &&
						MAX(sizes[0], sizes[1]) > tol/2);

				for (i = 0; i < num_segments_1; i++)
				{
					Vec3 dir1;
					F32 length1;
					subVec3(segments_1[i].points[4], segments_1[i].points[0], dir1);
					length1 = normalVec3(dir1);
					for (j = 0; j < num_segments_2; j++)
					{
						Vec3 dir2;
						F32 length2, distance;
						Vec3 isect1, isect2;
						subVec3(segments_2[j].points[4], segments_2[j].points[0], dir2);
						length2 = normalVec3(dir2);
						distance = LineLineDistSquared(	segments_1[i].points[0], dir1, length1, isect1,
														segments_2[j].points[0], dir2, length2, isect2);
						if (distance < min_distance)
						{
							F32 dist_1_A = distance3(isect1, segments_1[i].points[0]);
							F32 dist_1_B = dist_1_A + distance3(isect1, segments_1[i].points[4]);
							F32 t_interp_1 = dist_1_A / (dist_1_A + dist_1_B + 0.01f);
							F32 dist_2_A = distance3(isect2, segments_2[j].points[0]);
							F32 dist_2_B = dist_2_A + distance3(isect2, segments_2[j].points[4]);
							F32 t_interp_2 = dist_2_A / (dist_2_A + dist_2_B + 0.01f);

							min_distance = distance;
							copyVec3(isect1, collide_pos);
							copyVec3(dir1, collide_dir1);
							copyVec3(dir2, collide_dir2);
							*offset1 = idx1 + t_interp_1 * segments_1[i].t1 + (1-t_interp_1) * segments_1[i].t0;
							*offset2 = idx2 + t_interp_2 * segments_2[j].t1 + (1-t_interp_2) * segments_2[j].t0;
						}
					}
				}
				if (min_distance < tol*100)
					return true;
			}
		}
		start_offset2 = 0;
	}
	return false;
}

GroupDef *curveGetGeometry(const Mat4 parent_mat, GroupDef *def, Spline *spline, WorldCurveGap **gaps, Mat4 curve_matrix,
						   F32 uv_scale, F32 uv_rot, F32 stretch, int i, Mat4 out_mat, 
						   GroupSplineParams **params, F32 *distance_offset, F32 curve_scale)
{
    GroupDef *child_def = NULL;
    int index;
    F32 length;
	splineValidate(spline);
    assert(i >= 0 && i < eaiSize(&spline->spline_geom));
    index = spline->spline_geom[i];
    *params = NULL;
    if (index >= 0)
    {
		if (def->property_structs.child_curve && 
			(def->property_structs.child_curve->child_type == CURVE_CHILD_RANDOM ||
			def->property_structs.child_curve->child_type == CURVE_CHILD_OPTIMIZE))
		{
			GroupChild **children = groupGetChildren(def);
			if ( index >= 0 && index < eaSize(&children))
				child_def = groupChildGetDef(def, children[index], false);
		}
		else
		{
	        child_def = def;
		}
		if (!child_def)
			return NULL;

        length = curveGetGeometryLength(child_def, false);

		if (child_def)
        {
            // Get matrix
			int g;
            Vec3 up_vec, dir_vec, offset_vec;
            Mat4 orig_xform;
			F32 z_offset = (def->property_structs.child_curve && def->property_structs.child_curve->no_bounds_offset) ? 0 : -child_def->bounds.min[2];

			for (g = 0; g < eaSize(&gaps); g++)
			{
				Vec3 gap_pos;
				mulVecMat4(gaps[g]->position, curve_matrix, gap_pos);
				if (distance3(&spline->spline_points[i*3], gap_pos) < gaps[g]->radius)
					return NULL;
			}

            copyVec3(&spline->spline_up[i*3], up_vec);
            copyVec3(&spline->spline_deltas[i*3], dir_vec);
            normalVec3(dir_vec);
            if (!dir_vec[0] && !dir_vec[1] && !dir_vec[2])
            {
                identityMat4(orig_xform);
            }
            else
            {
	            orientMat3Yvec(*(Mat3*)&orig_xform, dir_vec, up_vec);
            }
            copyVec3(&spline->spline_points[i*3], orig_xform[3]);
			scaleVec3(dir_vec, z_offset, offset_vec);
			addVec3(orig_xform[3], offset_vec, orig_xform[3]);

			subVec3(orig_xform[3], curve_matrix[3], orig_xform[3]);
			scaleVec3(orig_xform[3], curve_scale, orig_xform[3]);
			addVec3(orig_xform[3], curve_matrix[3], orig_xform[3]);
            
            if (def->property_structs.child_curve && def->property_structs.child_curve->deform)
            {
                Mat4 bones[2];
                F32 scale = (uv_scale > 0.f) ? 1.f / uv_scale : 1.f;
                F32 cos_rot = cos(uv_rot*3.14159f/180.f) * scale;
                F32 sin_rot = sin(uv_rot*3.14159f/180.f) * scale;
                
                if (splinePointsAreIdentical(spline, i*3))
                    return NULL;

                copyMat4(orig_xform, out_mat);
                
				splineGetMatrices(parent_mat, curve_matrix[3], curve_scale, spline, i*3, bones, def->property_structs.child_curve ? def->property_structs.child_curve->linearize : false);

                *params = calloc(1, sizeof(GroupSplineParams));
                copyMat4(bones[0], (*params)->spline_matrices[0]);
                copyMat4(bones[1], (*params)->spline_matrices[1]);
                (*params)->spline_matrices[1][2][0] = length;
                (*params)->spline_matrices[1][2][1] = stretch * curve_scale;
                (*params)->spline_matrices[1][2][2] = *distance_offset;
                (*params)->spline_matrices[1][3][0] = distance3(&spline->spline_points[i*3], &spline->spline_points[i*3+3]);
                (*params)->spline_matrices[1][3][1] = cos_rot;
                (*params)->spline_matrices[1][3][2] = sin_rot;

                *distance_offset += (*params)->spline_matrices[1][3][0];
            }
            else
            {
                copyMat4(orig_xform, out_mat);
            }
        }
    }
    //scaleVec3(out_mat[3], curve_scale, out_mat[3]);
    return child_def;
}

F32 splineGetTotalLength(Spline *in)
{
	F32 total_length;
	int next_index;
	F32 next_t;
	Vec3 exact_point, exact_up, exact_tangent;
	F32 length_remainder = 0.f;

	splineGetNextPointEx(in, false, zerovec3, 0, eafSize(&in->spline_points)-3, 0, 0, 1000000.f, &next_index, &next_t, 
						&total_length, &length_remainder, exact_point, exact_up, exact_tangent);
	return total_length;
}

void splineResetRotation(Spline *spline, Mat4 out_rot)
{
	int i;
	Mat4 rot;
	Mat3 inv_rot;
	Mat4 spline_mat[2];

	splineGetMatrices(unitmat, zerovec3, 1, spline, 0, spline_mat, false);
	splineEvaluate(spline_mat, 0, zerovec3, rot[3], rot[1], rot[2]);
	crossVec3(rot[1], rot[2], rot[0]);
	normalVec3(rot[0]);
	crossVec3(rot[0], rot[1], rot[2]);
	normalVec3(rot[2]);

	copyMat3(rot, inv_rot);
	transposeMat3(inv_rot);

	for (i = 0; i < eafSize(&spline->spline_points); i += 3)
	{
		Vec3 point;
		// Rotate point
		subVec3(&spline->spline_points[i], rot[3], point);
		mulVecMat3(point, inv_rot, &spline->spline_points[i]);
		// Rotate up
		copyVec3(&spline->spline_up[i], point);
		mulVecMat3(point, inv_rot, &spline->spline_up[i]);
		// Rotate tangent
		copyVec3(&spline->spline_deltas[i], point);
		mulVecMat3(point, inv_rot, &spline->spline_deltas[i]);
	}
	
	copyMat4(rot, out_rot);
}