/***************************************************************************



***************************************************************************/

#include "ScratchStack.h"
#include "LineDist.h"

#include "wlCurve.h"
#include "WorldGridPrivate.h"

// -1 for max_dist skips distance check.
// If flatten is set, then y values are ignored in distance checks
// Returns distance
F32 splineGetNearestPoint(Spline *spline, Vec3 in_point, Vec3 out_point, S32 *out_index, F32 *out_t, F32 max_dist, bool flatten)
{
	int i;
	int best_i = -1;
	F32 best_t = 0;
	F32 smallest_distance = 1e8;

	for (i = 0; i < eafSize(&spline->spline_points)-3; i+= 3)
	{
		Vec3 coll_vec;
		Vec3 control_points[4];
		F32 dist;
		Vec3 point;
		F32 t, begin_t, last_t;
		Vec3 min, max;

		begin_t = -1;
		splineGetControlPoints(spline, i, control_points);
		splineGetBoundingBox(control_points, min, max, max_dist);
		copyVec3(in_point, coll_vec);
		if(flatten)
			coll_vec[1] = (min[1]+max[1])*0.5f;
		if (max_dist < 0 ||
			pointBoxCollision(coll_vec, min, max))
		{
			for (begin_t = 0; begin_t <= 1.f; begin_t += 0.5f)
			{
				// Newton's method
				int iter_count = 0;
				F32 d_p, d_dp;
				Vec3 point_p;
				Vec3 point_dp;

				t = begin_t;
				do {
					Vec3 delta;
					last_t = t;

					// Evaluate bezier point, derivative, second derivative
					bezierGetPoint3D_fast(control_points, t, point, point_p, point_dp);
					subVec3(point,in_point,delta);
					if(flatten)
					{
						delta[1] = 0;
						point_dp[1] = 0;
					}
					// First derivative of distance
					d_p = 2*point_p[0]*delta[0] +
						2*point_p[1]*delta[1] +
						2*point_p[2]*delta[2];
					// Second derivative of distance
					d_dp = 2*point_dp[0]*delta[0] + 2*point_p[0]*point_p[0] +
						2*point_dp[1]*delta[1] + 2*point_p[1]*point_p[1] +
						2*point_dp[2]*delta[2] + 2*point_p[2]*point_p[2];
					// Iterate t
					t -= d_p / d_dp;
					t = CLAMP(t, 0.0f, 1.0f);
				} while (fabs(t - last_t) > 0.01f && (iter_count++ < 8));

				bezierGetPoint3D(control_points, t, point);
				if(flatten)
					dist = sqrt(SQR(point[0] - in_point[0]) + SQR(point[2] - in_point[2]));
				else
					dist = distance3(point, in_point);
				if (best_i == -1 || dist < smallest_distance)
				{
					best_i = i;
					best_t = t;
					smallest_distance = dist;
					copyVec3(point, out_point);
				}
			}
		}
	}
	*out_index = best_i;
	*out_t = best_t;
	return smallest_distance;
}
